# Windows開発環境チェックリスト

## OS/SDK

- [ ] Windows 11 x64
- [ ] OS build確認
- [ ] Windows SDK 10.0.22000.0以上
- [ ] Media Foundation Virtual Camera headers/libs
- [ ] Camera privacy setting確認
- [ ] Developer Mode方針確認

## Visual Studio/C++

- [ ] installed Visual Studio version記録
- [ ] Desktop development with C++
- [ ] CMake tools
- [ ] Windows SDK component
- [ ] ATL/MFCはlibwebrtc/Chromium要求時のみ
- [ ] clang-cl availability if required
- [ ] Ninja
- [ ] Python
- [ ] Git
- [ ] NTFS free disk 100GB目安 for WebRTC checkout
- [ ] long paths設定確認

## libwebrtc

- [ ] depot_tools
- [ ] compatible commit選定
- [ ] commit SHA lock
- [ ] depot_tools revision lock
- [ ] GN args lock
- [ ] Release x64 build
- [ ] VP8
- [ ] Opus
- [ ] H.264 status
- [ ] smoke link
- [ ] repositoryへcheckout/binaryをcommitしない

## Windows Receiver

- [ ] CMake configure
- [ ] Debug build
- [ ] Release build
- [ ] `/W4`
- [ ] unit tests
- [ ] D3D11
- [ ] WinHTTP
- [ ] COM/MF initialization
- [ ] QR dependency license

## Virtual Camera

- [ ] Microsoft official sample取得/参照
- [ ] Media Source DLL x64
- [ ] dependency dump
- [ ] admin PowerShell
- [ ] dev install path
- [ ] COM CLSID
- [ ] install script
- [ ] uninstall script
- [ ] FrameServerMonitor debug方法
- [ ] FrameServer debug方法
- [ ] Camera app
- [ ] OBS optional

## Audio

- [ ] audio render devices
- [ ] VB-CABLE optional install
- [ ] `CABLE Input`確認
- [ ] `CABLE Output`確認
- [ ] speaker fallback
- [ ] recording/downstream test app

## Network

- [ ] Cloudflare staging URL
- [ ] Windows outbound HTTPS
- [ ] WebRTC UDP
- [ ] STUN
- [ ] TURN optional
- [ ] mobile QR test device
- [ ] same LAN test
- [ ] different NAT test

## Diagnostics

- [ ] log directory
- [ ] dump policy
- [ ] tokens/SDP redaction
- [ ] Task Manager/resource monitoring
- [ ] Wireshark policy optional
