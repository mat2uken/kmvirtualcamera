#include "../receiver/signaling/win_http_client.h"
#include <iostream>
#include <cassert>

int main() {
    std::wstring url = L"https://webrtc-bridge-signaling.mat2uken.workers.dev";
    std::cout << "Testing WinHttpClient with: https://webrtc-bridge-signaling.mat2uken.workers.dev" << std::endl;

    km::signaling::WinHttpClient client(url);
    auto resp = client.CreateSession("test-receiver-live");

    if (resp.has_value()) {
        std::cout << "[PASS] Session created successfully!" << std::endl;
        std::cout << "  SessionId: " << resp->sessionId << std::endl;
        std::cout << "  JoinUrl: " << resp->joinUrl << std::endl;
    } else {
        std::cout << "[FAIL] Failed to create session." << std::endl;
        return 1;
    }

    return 0;
}
