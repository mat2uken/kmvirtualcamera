# Known Issues and Operational Constraints

Last Verified: 2026-08-16

## Verified Constraints & Behaviors
1. **Non-Trickle ICE Gathering Time**:
   - Initial connection establishment waits for ICE gathering state `complete` before exchanging SDPs. This typically takes 500ms–2000ms on standard network interfaces.
2. **Virtual Camera Registration Elevation**:
   - Development registration of `VirtualCameraMediaSource.dll` (`install-vcam-dev.ps1`) writes to `HKCR\CLSID` and requires administrator privilege once during installation. The Receiver application itself runs under standard user privileges.
3. **VB-CABLE Endpoint Names**:
   - In Windows Audio settings, VB-CABLE playback device is named `CABLE Input (VB-Audio Virtual Cable)` and recording device is `CABLE Output (VB-Audio Virtual Cable)`.
4. **Session Fixed Lifetime**:
   - Sessions expire strictly 5 minutes (300 seconds) after creation regardless of polling traffic, and must be re-created via QR scan if reconnection is required.
