# KM Virtual Camera PoC

WebRTC-based low-latency camera & microphone bridge for Windows 11. Streams camera video and microphone audio from a mobile browser (Safari iOS / Chrome Android) into Windows 11 as a native **Media Foundation Virtual Camera** (720p30 NV12) and **Virtual Microphone** (VB-CABLE `CABLE Input`).

## Architecture & Technology Stack

- **Signaling Backend**: Cloudflare Worker built with **Hono** + SQLite-backed **Durable Objects**.
- **Browser Sender**: Lightweight frontend built with **VanJS** (0.9 kB) + **Open Props** pure CSS design tokens, bundled with Vite (10.79 kB JS / 4.69 kB gzip).
- **Windows Native Receiver**: C++20 desktop application using **`libdatachannel`** + **`mbedTLS`** (no libwebrtc dependency), Direct3D 11 video preview, WinHTTP, and Nayuki QR code generator.
- **Audio Output**: Direct WASAPI shared-mode playout into VB-CABLE `CABLE Input (VB-Audio Virtual Cable)`.
- **Windows 11 Virtual Camera**: Out-of-process COM Media Source DLL (`VirtualCameraMediaSource.dll`) registered via `MFCreateVirtualCamera`, communicating across an overlapped Named Pipe (`\\.\pipe\WebRtcBridge.VirtualCamera.v1`) with 64-byte `FrameHeader` and rational 30fps sample pacing.

---

## Quick Start Guide

### 1. Run Automated Test Suite

Run the full end-to-end test verification script (Vitest + CTest + Artifact validation):

```powershell
pwsh -File scripts/test_pipeline_e2e.ps1
```

### 2. Start Cloudflare Signaling Server (Local Dev)

```powershell
pwsh -File scripts/start_signaling.ps1
```

The signaling server starts at `http://127.0.0.1:8787`.

### 3. Register the Virtual Camera DLL (One-time, Run as Administrator)

Open PowerShell as Administrator:

```powershell
pwsh -File scripts/register_vcam.ps1
```

*(To unregister later: `pwsh -File scripts/unregister_vcam.ps1`)*

### 4. Run Windows Receiver Application

```powershell
pwsh -File scripts/run_receiver.ps1
```

1. The Windows Receiver window will launch and display a QR code with the Join URL.
2. Scan the QR code on your mobile browser (or open the URL).
3. Tap **送信開始 (Start Streaming)** on the browser.
4. The Windows Receiver automatically completes WebRTC handshaking via Non-Trickle ICE.
5. The live video preview displays in Direct3D 11, audio streams to `CABLE Input`, and the Windows 11 Virtual Camera activates.
