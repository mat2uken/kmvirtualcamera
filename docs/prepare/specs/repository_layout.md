# 推奨実装Repository構成

```text
webrtc-bridge/
├── README.md
├── IMPLEMENTATION_STATUS.md
├── KNOWN_ISSUES.md
├── SECURITY.md
├── THIRD_PARTY_NOTICES.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── signaling.md
│   ├── windows.md
│   ├── virtual-camera.md
│   ├── operations.md
│   └── adr/
├── cloud/
│   ├── package.json
│   ├── package-lock.json
│   ├── tsconfig.json
│   ├── wrangler.jsonc
│   ├── src/
│   │   ├── index.ts
│   │   ├── router.ts
│   │   ├── env.ts
│   │   ├── durable/
│   │   │   └── session-object.ts
│   │   ├── signaling/
│   │   │   ├── api.ts
│   │   │   ├── tokens.ts
│   │   │   ├── types.ts
│   │   │   └── validation.ts
│   │   ├── turn/
│   │   │   └── credentials.ts
│   │   └── http/
│   │       ├── errors.ts
│   │       ├── headers.ts
│   │       └── request-id.ts
│   ├── web/
│   │   ├── index.html
│   │   ├── sender.ts
│   │   ├── api.ts
│   │   ├── rtc.ts
│   │   ├── state.ts
│   │   └── styles.css
│   ├── public/
│   └── test/
├── windows/
│   ├── CMakeLists.txt
│   ├── cmake/
│   │   └── WebRtcExternal.cmake
│   ├── receiver/
│   │   ├── app/
│   │   ├── ui/
│   │   ├── signaling/
│   │   ├── rtc/
│   │   ├── media/
│   │   ├── virtual_camera/
│   │   ├── platform/
│   │   └── config/
│   ├── virtual-camera/
│   │   ├── media-source/
│   │   ├── registrar/
│   │   ├── common/
│   │   └── tests/
│   ├── tests/
│   └── third_party/
│       ├── README.md
│       └── webrtc-lock.json
├── scripts/
│   ├── bootstrap-cloud.ps1
│   ├── build-libwebrtc.ps1
│   ├── build-windows.ps1
│   ├── test-all.ps1
│   ├── install-vcam-dev.ps1
│   ├── uninstall-vcam-dev.ps1
│   └── deploy-cloudflare.ps1
├── packaging/
│   └── README.md
└── .github/
    └── workflows/
        ├── cloud.yml
        └── windows.yml
```

## Dependency policy

### Commitしてよい

- project source
- package lock
- small QR encoder source + license
- build scripts
- patches
- lock metadata

### Commitしない

- full libwebrtc checkout
- depot_tools checkout
- generated WebRTC binaries
- secrets
- user config
- logs
- SDP dump
- VB-CABLE installer
- Microsoft sample repository全体の不要なcopy

## libwebrtc lock例

```json
{
  "source": "https://webrtc.googlesource.com/src",
  "commit": "<full-sha>",
  "depotToolsCommit": "<full-sha-or-null>",
  "target": "windows-x64",
  "gnArgs": {
    "is_debug": false,
    "is_component_build": false,
    "rtc_include_tests": false,
    "rtc_build_examples": false,
    "rtc_use_h264": true
  },
  "visualStudio": "<version>",
  "windowsSdk": "<version>",
  "verifiedAt": "2026-08-16",
  "notes": "Flags are revision-specific; this file is illustrative."
}
```

実際のGN arg名は固定revisionのbuild filesで検証する。
