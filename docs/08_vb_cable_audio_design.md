# 08. VB-CABLEを利用した音声出力設計

## 1. 目的

自社の仮想マイクドライバーを作成せず、WebRTCで受信したremote audioを既存の仮想オーディオケーブルへ流す。

典型的なVB-CABLEの流れ:

```text
Windows Receiver
  → playback/render device: "CABLE Input (VB-Audio Virtual Cable)"
  → VB-CABLE内部
  → recording/capture device: "CABLE Output (VB-Audio Virtual Cable)"
  → Teams / Zoom / Browser等のmicrophone
```

名称が直感と逆に見えるため、UIとドキュメントで明示する。

## 2. 配布上の扱い

- VB-CABLEを本リポジトリやinstallerへ同梱しない。
- VB-CABLEのlicense/利用条件は利用者が別途確認する。
- Receiverは特定ベンダー製品の存在を必須にせず、任意のWindows render endpointを選択可能にする。
- BlackHole等の他OS製品は対象外。
- Windows上の他のvirtual audio cableにも一般化できる設計にする。

## 3. 基本実装

native libwebrtcのAudioDeviceModuleでplayout deviceを選択する。

起動時:

1. render devices列挙
2. friendly name、device identifierをUIへ表示
3. configの保存IDと一致すれば選択
4. 一致しなければ名前substringで`CABLE Input`候補
5. ユーザー確認
6. playout初期化
7. PeerConnectionFactory/AudioDeviceModuleへ反映

libwebrtc revisionごとにAudioDeviceModule APIが変わる可能性があるため、実装時に固定revisionのheaderを正とする。

## 4. Device selector

UI表示:

```text
Audio output:
  CABLE Input (VB-Audio Virtual Cable)
  Speakers (Realtek ...)
  Headphones (...)
```

内部保存:

```json
{
  "preferredAudioOutput": {
    "deviceId": "...",
    "friendlyNameFallback": "CABLE Input"
  }
}
```

- deviceIdを優先
- friendlyNameはfallback
- indexだけを永続化しない
- disabled/unplugged deviceを除外または状態表示
- device changeを検出し、再選択を促す

## 5. Sample rate/channel

WebRTC音声処理は通常48kHzを中心に動く。VB-CABLE側formatとWindows shared-mode audio engineが変換する場合がある。

初期版:

- WASAPI shared mode相当
- 48kHz
- 2chまたはendpoint mix format
- exclusive modeなし
- manual clock synchronizationなし

音声が無音・速度異常になる場合:

- endpoint mix format確認
- AudioDeviceModule callback format確認
- resampler経路確認
- channel mapping確認
- Windows sound control panelのdefault format確認

## 6. 二重再生防止

VB-CABLEへ出力しつつスピーカーにも同時出力するとechoやfeedbackの原因になる。

v1:

- playout endpointは1つだけ選択
- monitoring speakerへの複製は実装しない
- browser senderが同じ音を拾う環境ではecho cancellationをbrowser側defaultに任せる
- Receiver自身のmicrophone captureは行わない

将来monitoringを追加する場合は明示的opt-inとする。

## 7. Fallback: AudioTrackSink + WASAPI

固定libwebrtc revisionでAudioDeviceModuleのdevice selectionが困難な場合に限り採用する。

```text
webrtc::AudioTrackInterface
  → AudioTrackSinkInterface::OnData
  → bounded PCM ring
  → WASAPI shared-mode render client
  → selected endpoint
```

要件:

- WebRTC callbackをWASAPI writeでblockしない
- bounded ring buffer
- overflow時は古いaudioを無制限に溜めない
- underflowはsilence
- format conversion/resamplingを明示
- default ADM playoutを無効化し二重再生を防ぐ
- clock driftを計測
- endpoint invalidationをhandle

これは初期優先経路ではない。採用時はADRへ理由を書く。

## 8. Error handling

| 状態 | 動作 |
|---|---|
| VB-CABLE未導入 | 通常speakerを選べる。仮想micは使えない旨を表示 |
| device disabled | 選択不可表示 |
| device removed | playout停止、再選択 |
| init failure | error codeとendpointを診断へ |
| format failure | fallback formatまたは明示エラー |
| no remote audio | 接続済みでも無音状態を表示 |
| audio track ended | playout停止 |

## 9. 利用者向け案内

Windows target app側では:

- Camera: `WebRTC Bridge Windows Virtual Camera`
- Microphone: `CABLE Output (VB-Audio Virtual Cable)`

Receiver側では:

- Audio output: `CABLE Input (VB-Audio Virtual Cable)`

この対応をUIヘルプへ常時表示する。

## 10. テスト

- VB-CABLEなし
- VB-CABLEあり
- speakerへ出力
- endpoint切替
- 接続中device disable
- 48kHz mono/stereo remote
- 30分連続
- audio dropout
- browser mute/unmute
- browser tab close
- downstream appでCABLE Output録音
- downstream appのmeterでsignal確認
- echo/feedbackが起きないこと
