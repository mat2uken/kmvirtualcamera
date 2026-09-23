#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace km {
enum class TimestampDomain { Rtp90kHz, SenderMicroseconds };
struct EncodedVideoFrame {
    std::vector<uint8_t> annexB;
    int64_t mediaTicks=0;
    TimestampDomain timestampDomain=TimestampDomain::Rtp90kHz;
    uint64_t receivedMonotonicNs=0, generation=0;
    bool randomAccess=false;
};
struct OutputFormat { int width=1280,height=720; uint32_t fpsNumerator=30,fpsDenominator=1; };
struct Transform { int clockwiseRotation=0; };
enum class SubmitResult { Accepted, Backpressure, NeedKeyframe, Stopped, Error };
// Serial owner executor; stop invalidates all queued work before return.
// Native decoded surfaces stay behind this boundary, never copied into a common byte vector.
class IVideoPipeline {
public:
    virtual ~IVideoPipeline()=default;
    virtual bool start(OutputFormat format, uint64_t generation, std::string& error)=0;
    virtual SubmitResult submit(EncodedVideoFrame frame)=0;
    virtual void setTransform(Transform transform)=0;
    virtual void stop()=0;
};
struct HttpRequest {
    std::string method,url,body;
    std::vector<std::pair<std::string,std::string>> headers;
    uint32_t timeoutMs=10000;
    size_t maxResponseBytes=1024*1024;
};
struct HttpResponse {
    int status=0;
    std::string body,error;
    std::vector<std::pair<std::string,std::string>> headers;
};
// Blocking only on the signaling worker, never main/UI, RTC callbacks or video worker.
// Production adapter must enforce timeouts, HTTPS validation and response size bounds.
class IHttpTransport {
public:
    virtual ~IHttpTransport()=default;
    virtual HttpResponse perform(const HttpRequest& request)=0;
    virtual void cancel()=0;
};
} // namespace km
