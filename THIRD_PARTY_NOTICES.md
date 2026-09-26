# Third-Party Notices

This project incorporates open source software with the following licenses:

## 1. libdatachannel
- **Description**: C/C++ WebRTC network library (PeerConnection, DataChannel, Media).
- **License**: Mozilla Public License Version 2.0 (MPL-2.0)
- **Source**: https://github.com/paullouisageneau/libdatachannel
- **Pinned version**: `v0.22.4`.
- **Local modification**: `patches/libdatachannel-0.22.4-defer-demux.patch`, applied by the build through `cmake/apply_libdatachannel_patch.cmake`. It defers SRTP/RTCP dispatch until the DTLS SSL mutex is released. Reason, scope and evidence are recorded in `plans/macos-virtual-camera/02-03-windows-native.md`.

## 2. Mbed TLS
- **Description**: Lightweight cryptographic and TLS library.
- **License**: Apache License 2.0
- **Source**: https://github.com/Mbed-TLS/mbedtls

## 3. libopus
- **Description**: IETF Opus audio codec reference implementation.
- **License**: BSD 3-Clause License
- **Source**: https://github.com/xiph/opus

## 4. QR Code generator (Nayuki)
- **Description**: High quality, compact QR Code generator library in C++.
- **License**: MIT License
- **Source**: https://github.com/nayuki/QR-Code-generator

## 5. Hono
- **Description**: Ultrafast web framework for Cloudflare Workers.
- **License**: MIT License
- **Source**: https://github.com/honojs/hono

## 6. VanJS
- **Description**: 0.9kB Reactive UI library.
- **License**: MIT License
- **Source**: https://github.com/vanjs-org/van

## 7. Open Props
- **Description**: Modern CSS custom properties.
- **License**: MIT License
- **Source**: https://github.com/argyleink/open-props
