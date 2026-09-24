#include "url_session_transport.h"
#import <Foundation/Foundation.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string_view>

// Delegate trampoline is declared up front: the C++ transport constructs it.
@interface KmUrlSessionDelegate : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithOwner:(void*)owner;
@end

namespace km::mac {
namespace {
std::string FromNs(NSString* value) {
    if (!value) return {};
    const char* utf8 = value.UTF8String;
    return utf8 ? std::string(utf8) : std::string();
}
bool SafeHeaderValue(std::string_view value) {
    for (unsigned char c : value)
        if (c < 32 || c == 127) return false;
    return true;
}

// Blocking transport state shared with the Objective-C delegate below. One
// request in flight at a time: the contract serializes callers on the
// signaling worker, and cancel() may race from another thread. Result
// semantics match WinHttpTransport: whenever error is set, status is 0 and
// the body is discarded.
class UrlSessionTransport final : public km::IHttpTransport {
public:
    UrlSessionTransport() {
        @autoreleasepool {
            delegate_ = [[KmUrlSessionDelegate alloc] initWithOwner:this];
            NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
            config.URLCache = nil;
            config.HTTPCookieStorage = nil;
            NSOperationQueue* queue = [[NSOperationQueue alloc] init];
            queue.maxConcurrentOperationCount = 1;
            session_ = [NSURLSession sessionWithConfiguration:config delegate:delegate_ delegateQueue:queue];
        }
    }
    ~UrlSessionTransport() override {
        std::unique_lock lock(mutex_);
        if (inflight_) {
            status_ = 0;
            body_.clear();
            error_ = "HTTP transport shutting down";
            failed_ = true;
            [task_ cancel];
            ready_.wait(lock, [this] { return !inflight_; });
        }
        lock.unlock();
        // After the wait above no delegate callback can still reference `this`.
        [session_ invalidateAndCancel];
    }

    void cancel() override {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        if (inflight_ && !failed_) {
            status_ = 0;
            body_.clear();
            error_ = "HTTP request cancelled";
            failed_ = true;
            [task_ cancel];
        }
    }

    km::HttpResponse perform(const km::HttpRequest& request) override {
        @autoreleasepool {
            std::unique_lock lock(mutex_);
            if (cancelled_) return Fail("HTTP request cancelled");
            if (inflight_) return Fail("HTTP request busy");
            if (!request.timeoutMs || request.body.size() > 1024 * 1024 ||
                request.url.size() > 8192 || request.url.find('#') != std::string::npos)
                return Fail("invalid HTTP request limits or URL");

            NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
            if (!url || url.scheme.length == 0 || url.host.length == 0 || url.user || url.password)
                return Fail("invalid signaling URL");
            const std::string scheme = FromNs(url.scheme.lowercaseString);
            const std::string host = FromNs(url.host.lowercaseString);
            const bool loopback = host == "localhost" || host == "127.0.0.1" || host == "::1";
            if (scheme != "https" && !(scheme == "http" && loopback))
                return Fail("signaling requires HTTPS (except explicit loopback development)");

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
            req.HTTPMethod = [NSString stringWithUTF8String:request.method.c_str()];
            req.timeoutInterval = double(request.timeoutMs) / 1000.0;
            req.HTTPShouldHandleCookies = NO;
            if (!request.body.empty())
                req.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];
            for (const auto& [name, value] : request.headers) {
                if (name.empty() || name.find_first_of(": \t") != std::string::npos ||
                    !SafeHeaderValue(name) || !SafeHeaderValue(value))
                    return Fail("invalid HTTP header");
                [req setValue:[NSString stringWithUTF8String:value.c_str()]
                    forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
            }

            status_ = 0;
            body_.clear();
            error_.clear();
            headers_.clear();
            maxBytes_ = request.maxResponseBytes;
            failed_ = false;
            inflight_ = true;
            task_ = [session_ dataTaskWithRequest:req];
            [task_ resume];

            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(request.timeoutMs);
            if (!ready_.wait_until(lock, deadline, [this] { return !inflight_; })) {
                // NSURLSession's own timeout usually wins; this is the backstop.
                status_ = 0;
                body_.clear();
                error_ = "HTTP deadline exceeded";
                failed_ = true;
                [task_ cancel];
                ready_.wait(lock, [this] { return !inflight_; });
            }
            task_ = nil;
            km::HttpResponse out;
            out.status = status_;
            out.body = std::move(body_);
            out.headers = std::move(headers_);
            out.error = std::move(error_);
            body_.clear();
            error_.clear();
            if (!out.error.empty()) { out.status = 0; out.body.clear(); }
            return out;
        }
    }

    // --- delegate entry points (delegate queue thread) ---
    bool OnResponse(NSURLSessionTask* task, NSHTTPURLResponse* response, int64_t declaredBytes) {
        std::lock_guard lock(mutex_);
        if (!inflight_ || task != task_ || failed_) return true;
        status_ = int(response.statusCode);
        for (NSString* key in response.allHeaderFields)
            headers_.emplace_back(FromNs(key), FromNs(response.allHeaderFields[key]));
        if (declaredBytes >= 0 && std::size_t(declaredBytes) > maxBytes_) {
            error_ = "HTTP response too large";
            failed_ = true;
            return true;
        }
        return false;
    }
    bool OnData(NSURLSessionTask* task, const uint8_t* bytes, size_t length) {
        std::lock_guard lock(mutex_);
        if (!inflight_ || task != task_ || failed_) return true;
        if (length > maxBytes_ - body_.size()) {
            error_ = "HTTP response too large";
            failed_ = true;
            return true;
        }
        body_.append(reinterpret_cast<const char*>(bytes), length);
        return false;
    }
    void OnComplete(NSURLSessionTask* task, NSError* error) {
        std::lock_guard lock(mutex_);
        if (task != task_) return;
        if (error && !failed_) {
            status_ = 0;
            body_.clear();
            if (error.code == NSURLErrorCancelled) error_ = "HTTP request cancelled";
            else if (error.code == NSURLErrorTimedOut) error_ = "HTTP deadline exceeded";
            else error_ = "HTTP request failed: " + FromNs(error.localizedDescription);
            failed_ = true;
        }
        inflight_ = false;
        ready_.notify_all();
    }

private:
    static km::HttpResponse Fail(const char* message) {
        km::HttpResponse out;
        out.error = message;
        return out;
    }
    std::mutex mutex_;
    std::condition_variable ready_;
    NSURLSession* session_ = nil;
    id delegate_ = nil;
    NSURLSessionDataTask* task_ = nil;
    bool inflight_ = false, failed_ = false, cancelled_ = false;
    int status_ = 0;
    size_t maxBytes_ = 0;
    std::string body_, error_;
    std::vector<std::pair<std::string, std::string>> headers_;
};
} // namespace
} // namespace km::mac

@implementation KmUrlSessionDelegate {
    void* _owner;
}
- (instancetype)initWithOwner:(void*)owner {
    if ((self = [super init])) _owner = owner;
    return self;
}
- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
willPerformHTTPRedirection:(NSHTTPURLResponse*)response
        newRequest:(NSURLRequest*)request
 completionHandler:(void (^)(NSURLRequest*))completionHandler {
    (void)session;
    (void)task;
    (void)response;
    (void)request;
    completionHandler(nil); // never follow redirects: surface the 3xx as-is
}
- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
didReceiveResponse:(NSURLResponse*)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    (void)session;
    @autoreleasepool {
        auto* owner = static_cast<km::mac::UrlSessionTransport*>(_owner);
        const bool cancel = owner->OnResponse(dataTask, (NSHTTPURLResponse*)response,
            response.expectedContentLength);
        completionHandler(cancel ? NSURLSessionResponseCancel : NSURLSessionResponseAllow);
    }
}
- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveData:(NSData*)data {
    (void)session;
    @autoreleasepool {
        auto* owner = static_cast<km::mac::UrlSessionTransport*>(_owner);
        const bool cancel = owner->OnData(dataTask,
            static_cast<const uint8_t*>(data.bytes), data.length);
        if (cancel) [dataTask cancel];
    }
}
- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
didCompleteWithError:(NSError*)error {
    (void)session;
    @autoreleasepool {
        static_cast<km::mac::UrlSessionTransport*>(_owner)->OnComplete(task, error);
    }
}
@end

namespace km::mac {
std::unique_ptr<km::IHttpTransport> MakeUrlSessionTransport() {
    return std::make_unique<UrlSessionTransport>();
}

MacHttpClient::MacHttpClient(std::string baseUrl) {
    const auto first = baseUrl.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) throw std::invalid_argument("empty signaling URL");
    const auto last = baseUrl.find_last_not_of(" \t\r\n\"'");
    transport_ = MakeUrlSessionTransport();
    client_ = std::make_unique<km::signaling::SessionClient>(*transport_,
        baseUrl.substr(first, last - first + 1));
}
std::optional<km::signaling::CreateSessionResponse> MacHttpClient::CreateSession(const std::string& name) {
    return client_->CreateSession(name);
}
std::optional<km::signaling::OfferDescription> MacHttpClient::PollOffer(const std::string& id, const std::string& token) {
    return client_->PollOffer(id, token);
}
bool MacHttpClient::PutAnswer(const std::string& id, const std::string& token, const std::string& sdp) {
    return client_->PutAnswer(id, token, sdp);
}
bool MacHttpClient::DeleteSession(const std::string& id, const std::string& token) {
    return client_->DeleteSession(id, token);
}
void MacHttpClient::Cancel() { transport_->cancel(); }
} // namespace km::mac
