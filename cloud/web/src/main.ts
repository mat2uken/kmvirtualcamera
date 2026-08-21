import van from "vanjs-core";
import { SignalingClient } from "./api";
import { WebRtcSender } from "./rtc";

const { div, h1, span, select, option, button, label, video, details, summary, pre } = van.tags;

function parseFragment(): { sessionId: string; joinToken: string } | null {
  const hash = window.location.hash.slice(1);
  if (!hash) {
    const savedSession = sessionStorage.getItem("wrtc.sessionId");
    const savedToken = sessionStorage.getItem("wrtc.joinToken");
    if (savedSession && savedToken) {
      return { sessionId: savedSession, joinToken: savedToken };
    }
    return null;
  }

  const params = new URLSearchParams(hash);
  const sessionId = params.get("s");
  const joinToken = params.get("j");

  if (sessionId && joinToken) {
    sessionStorage.setItem("wrtc.sessionId", sessionId);
    sessionStorage.setItem("wrtc.joinToken", joinToken);
    // Sanitize URL bar immediately
    history.replaceState(null, "", window.location.pathname);
    return { sessionId, joinToken };
  }

  return null;
}

function App() {
  const status = van.state<"idle" | "requesting_media" | "connecting" | "connected" | "error">("idle");
  const statusText = van.state<string>("準備完了");
  const errorMessage = van.state<string>("");
  const diagnostics = van.state<string>("Ready.");

  const videoDevices = van.state<{ id: string; label: string }[]>([]);
  const audioDevices = van.state<{ id: string; label: string }[]>([]);
  const selectedVideo = van.state<string>("");
  const selectedAudio = van.state<string>("");
  const facingMode = van.state<"user" | "environment">("environment");

  // Quality & Encoding settings
  const selectedResolution = van.state<string>("1280x720");
  const selectedFps = van.state<number>(30);
  const selectedBitrate = van.state<number>(2500000); // 2.5 Mbps default (stable 30fps)

  const parsed = parseFragment();
  const sessionId = van.state<string>(parsed?.sessionId || "");
  const joinToken = van.state<string>(parsed?.joinToken || "");

  const api = new SignalingClient();
  const rtc = new WebRtcSender();
  const videoElem = video({ class: "preview-video", playsinline: true, muted: true, autoplay: true });

  const getTargetDimensions = () => {
    const [w, h] = selectedResolution.val.split("x").map(Number);
    return { width: w || 1280, height: h || 720 };
  };

  const updateDevices = async () => {
    try {
      const devices = await navigator.mediaDevices.enumerateDevices();
      const v = devices
        .filter((d) => d.kind === "videoinput")
        .map((d, i) => ({ id: d.deviceId, label: d.label || (d.deviceId ? `カメラ ${i + 1}` : "カメラ") }));
      const a = devices
        .filter((d) => d.kind === "audioinput")
        .map((d, i) => ({ id: d.deviceId, label: d.label || (d.deviceId ? `マイク ${i + 1}` : "マイク") }));
      videoDevices.val = v;
      audioDevices.val = a;

      const activeVideoSettings = rtc.getActiveVideoTrackSettings();
      if (activeVideoSettings?.deviceId && v.some((d) => d.id === activeVideoSettings.deviceId)) {
        selectedVideo.val = activeVideoSettings.deviceId;
      } else if (v.length > 0 && (!selectedVideo.val || !v.some((d) => d.id === selectedVideo.val))) {
        selectedVideo.val = v[0].id;
      }

      const activeAudioSettings = rtc.getActiveAudioTrackSettings();
      if (activeAudioSettings?.deviceId && a.some((d) => d.id === activeAudioSettings.deviceId)) {
        selectedAudio.val = activeAudioSettings.deviceId;
      } else if (a.length > 0 && (!selectedAudio.val || !a.some((d) => d.id === selectedAudio.val))) {
        selectedAudio.val = a[0].id;
      }
    } catch {
      // Ignore if permission not granted yet
    }
  };

  updateDevices();
  if (navigator.mediaDevices && typeof navigator.mediaDevices.addEventListener === "function") {
    navigator.mediaDevices.addEventListener("devicechange", updateDevices);
  }

  // Dynamic Camera Switch Handler
  const handleVideoChange = async (newVideoId: string) => {
    selectedVideo.val = newVideoId;
    const { width, height } = getTargetDimensions();
    addLog(`カメラ切り替え要求: ${newVideoId || "デフォルト"}`);

    if (status.val === "connected" || status.val === "connecting" || status.val === "requesting_media") {
      try {
        const stream = await rtc.switchVideo(newVideoId, width, height, selectedFps.val);
        (videoElem as HTMLVideoElement).srcObject = stream;
        await updateDevices();
        addLog("カメラ切り替え完了");
      } catch (err) {
        console.error("Failed to switch video:", err);
        addLog(`カメラ切り替えエラー: ${err}`);
      }
    }
  };

  // Dynamic Microphone Switch Handler
  const handleAudioChange = async (newAudioId: string) => {
    selectedAudio.val = newAudioId;
    addLog(`マイク切り替え要求: ${newAudioId || "デフォルト"}`);

    if (status.val === "connected" || status.val === "connecting" || status.val === "requesting_media") {
      try {
        const stream = await rtc.switchAudio(newAudioId);
        (videoElem as HTMLVideoElement).srcObject = stream;
        await updateDevices();
        addLog("マイク切り替え完了");
      } catch (err) {
        console.error("Failed to switch audio:", err);
        addLog(`マイク切り替えエラー: ${err}`);
      }
    }
  };

  // Flip Camera (Toggle between front / back camera)
  const handleFlipCamera = async () => {
    const { width, height } = getTargetDimensions();
    addLog("📷 カメラ切り替え (Flip) 実行...");

    // Toggle facingMode state
    const nextFacing = facingMode.val === "environment" ? "user" : "environment";
    facingMode.val = nextFacing;

    const validDevices = videoDevices.val.filter((d) => d.id);
    if (validDevices.length > 1) {
      const curIdx = validDevices.findIndex((d) => d.id === selectedVideo.val);
      const nextIdx = (curIdx + 1) % validDevices.length;
      const nextDevice = validDevices[nextIdx];
      selectedVideo.val = nextDevice.id;
      addLog(`次のカメラへ切り替え: ${nextDevice.label}`);

      if (status.val === "connected" || status.val === "connecting" || status.val === "requesting_media") {
        try {
          const stream = await rtc.switchVideo(nextDevice.id, width, height, selectedFps.val);
          (videoElem as HTMLVideoElement).srcObject = stream;
          await updateDevices();
        } catch (err) {
          console.warn("Flip camera by id failed, falling back to facingMode:", err);
          try {
            const stream = await rtc.switchVideo(nextFacing, width, height, selectedFps.val);
            (videoElem as HTMLVideoElement).srcObject = stream;
            await updateDevices();
          } catch {}
        }
      }
    } else {
      addLog(`facingMode 切り替え: ${nextFacing === "user" ? "インカメラ" : "アウトカメラ"}`);
      if (status.val === "connected" || status.val === "connecting" || status.val === "requesting_media") {
        try {
          const stream = await rtc.switchVideo(nextFacing, width, height, selectedFps.val);
          (videoElem as HTMLVideoElement).srcObject = stream;
          await updateDevices();
        } catch (err) {
          console.error("Flip camera by facingMode failed:", err);
          addLog(`facingMode 切り替えエラー: ${err}`);
        }
      }
    }
  };

  const openDiagnostics = van.state<boolean>(false);
  const copyStatus = van.state<string>("📋 診断ログをコピー");

  const ts = () => {
    const d = new Date();
    return d.toTimeString().split(" ")[0];
  };

  const addLog = (msg: string) => {
    diagnostics.val += `\n[${ts()}] ${msg}`;
  };

  const handleCopyLogs = async () => {
    try {
      await navigator.clipboard.writeText(diagnostics.val);
      copyStatus.val = "✓ コピー完了！";
      setTimeout(() => {
        copyStatus.val = "📋 診断ログをコピー";
      }, 3000);
    } catch {
      copyStatus.val = "コピー失敗";
    }
  };

  const handleStart = async () => {
    if (!sessionId.val || !joinToken.val) {
      status.val = "error";
      errorMessage.val = "有効なQRコードまたは接続URLからアクセスしてください。";
      return;
    }

    try {
      status.val = "requesting_media";
      statusText.val = "カメラ・マイクを取得中...";
      errorMessage.val = "";
      addLog("カメラ・マイクの取得を開始...");

      const { width, height } = getTargetDimensions();
      const initialVideo = selectedVideo.val || facingMode.val;
      const stream = await rtc.getMedia(initialVideo, selectedAudio.val, width, height, selectedFps.val);
      (videoElem as HTMLVideoElement).srcObject = stream;
      await updateDevices();
      addLog(`カメラ取得完了 (${width}x${height}, ${selectedFps.val}fps)`);

      status.val = "connecting";
      statusText.val = "セッション登録中...";

      let nonce = sessionStorage.getItem("wrtc.nonce");
      if (!nonce) {
        nonce = crypto.randomUUID();
        sessionStorage.setItem("wrtc.nonce", nonce);
      }

      addLog(`Signaling claim セッション登録中... (Session: ${sessionId.val.slice(0, 8)})`);
      const claimRes = await api.claim(sessionId.val, joinToken.val, nonce);
      addLog(`Session claimed. 有効期限: ${claimRes.expiresAt}, STUNサーバー数: ${claimRes.rtcConfiguration.iceServers?.length || 0}`);

      statusText.val = "Offer SDP生成・ICE収集中...";
      const offerSdp = await rtc.createPeerConnection(
        claimRes.rtcConfiguration,
        (pcState) => {
          addLog(`PeerConnection state: ${pcState}`);
          if (pcState === "connected") {
            status.val = "connected";
            statusText.val = "接続中 (送信中)";
            errorMessage.val = "";
          } else if (pcState === "connecting") {
            statusText.val = "WebRTC接続確立中...";
          } else if (pcState === "disconnected") {
            statusText.val = "接続再試行中...";
          } else if (pcState === "failed" || pcState === "closed") {
            status.val = "error";
            statusText.val = "接続失敗";
            errorMessage.val = `WebRTC接続に失敗しました (${pcState})。同一Wi-Fiに接続するか、再接続をお試しください。`;
            openDiagnostics.val = true;
          }
        },
        (stats) => {
          addLog(`Stats: ${JSON.stringify(stats)}`);
        },
        selectedBitrate.val,
        selectedFps.val,
        (iceState) => {
          addLog(`ICE state: ${iceState}`);
          if (iceState === "failed") {
            status.val = "error";
            statusText.val = "接続失敗";
            errorMessage.val = "ICE接続に失敗しました。Windows側と同一Wi-Fiに接続するか、ファイアウォールをご確認ください。";
            openDiagnostics.val = true;
          }
        },
        (diagMsg) => {
          addLog(diagMsg);
        }
      );

      statusText.val = "Offer送信中...";
      addLog(`Offer SDP送信中 (${offerSdp.length} bytes)...`);
      await api.putOffer(sessionId.val, claimRes.senderToken, offerSdp);
      addLog("Offer SDP送信完了。Windows側Answer待機中...");

      statusText.val = "Windows側Answer待機中 (ポーリング)...";
      const answerSdp = await api.pollAnswer(sessionId.val, claimRes.senderToken, claimRes.poll);
      addLog(`Answer SDP受信 (${answerSdp.length} bytes)`);

      statusText.val = "接続確立中 (ICE接続)...";
      await rtc.setAnswer(answerSdp, addLog);
      addLog("Answer SDP設定完了。P2P接続確立待機中...");
    } catch (err: unknown) {
      status.val = "error";
      statusText.val = "接続失敗";
      const msg = err instanceof Error ? err.message : String(err);
      errorMessage.val = msg;
      addLog(`Error: ${msg}`);
      openDiagnostics.val = true;
      rtc.stop();
    }
  };

  const handleStop = async () => {
    rtc.stop();
    (videoElem as HTMLVideoElement).srcObject = null;
    status.val = "idle";
    statusText.val = "停止";
    addLog("送信停止しました。");
  };

  return div(
    { class: "container" },
    div(
      { class: "header" },
      h1({ class: "title" }, "KM Virtual Camera"),
      () =>
        span(
          {
            class: `badge badge-${
              status.val === "connected" ? "connected" : status.val === "error" ? "error" : status.val === "idle" ? "idle" : "connecting"
            }`
          },
          statusText.val
        )
    ),

    div(
      { class: "video-wrapper" },
      videoElem,
      () =>
        status.val === "idle" && !(videoElem as HTMLVideoElement).srcObject
          ? div({ class: "video-placeholder" }, "「送信開始」を押すとカメラ映像が表示されます")
          : div()
    ),

    () =>
      errorMessage.val
        ? div(
            { class: "message-box message-error" },
            div(errorMessage.val),
            div(
              { style: "display: flex; gap: 8px; margin-top: 8px;" },
              button(
                {
                  class: "button button-primary",
                  style: "padding: 6px 12px; font-size: 14px;",
                  onclick: handleStart
                },
                "🔄 再接続"
              ),
              button(
                {
                  class: "button button-secondary",
                  style: "padding: 6px 12px; font-size: 14px;",
                  onclick: handleCopyLogs
                },
                () => copyStatus.val
              )
            )
          )
        : div(),

    div(
      { class: "controls" },
      // Camera Selection with Flip Button
      div(
        { class: "form-row" },
        div(
          { class: "form-group" },
          label({ class: "label" }, "カメラ選択"),
          () =>
            select(
              {
                class: "select",
                value: selectedVideo.val,
                onchange: (e: Event) => handleVideoChange((e.target as HTMLSelectElement).value)
              },
              videoDevices.val.length > 0
                ? videoDevices.val.map((d) =>
                    option({ value: d.id, selected: d.id === selectedVideo.val }, d.label)
                  )
                : [option({ value: "" }, "カメラ (検出中または未接続)")]
            )
        ),
        button(
          {
            class: "button button-secondary",
            onclick: handleFlipCamera,
            title: "イン/アウトカメラ切り替え"
          },
          "📷 切替"
        )
      ),

      // Microphone Selection
      div(
        { class: "form-group" },
        label({ class: "label" }, "マイク選択"),
        () =>
          select(
            {
              class: "select",
              value: selectedAudio.val,
              onchange: (e: Event) => handleAudioChange((e.target as HTMLSelectElement).value)
            },
            audioDevices.val.length > 0
              ? audioDevices.val.map((d) =>
                  option({ value: d.id, selected: d.id === selectedAudio.val }, d.label)
                )
              : [option({ value: "" }, "マイク (検出中または未接続)")]
          )
      ),

      // Video Quality Settings
      div(
        { class: "settings-group" },
        div({ class: "settings-title" }, "⚙️ 送信画質設定"),
        div(
          { class: "settings-grid" },
          div(
            { class: "form-group" },
            label({ class: "label" }, "解像度"),
            select(
              {
                class: "select",
                disabled: () => status.val === "connected" || status.val === "connecting",
                onchange: (e: Event) => (selectedResolution.val = (e.target as HTMLSelectElement).value)
              },
              option({ value: "1920x1080", selected: selectedResolution.val === "1920x1080" }, "1080p (Full HD)"),
              option({ value: "1280x720", selected: selectedResolution.val === "1280x720" }, "720p (HD 推奨)"),
              option({ value: "854x480", selected: selectedResolution.val === "854x480" }, "480p (SD)"),
              option({ value: "640x360", selected: selectedResolution.val === "640x360" }, "360p (軽量)")
            )
          ),
          div(
            { class: "form-group" },
            label({ class: "label" }, "フレームレート"),
            select(
              {
                class: "select",
                disabled: () => status.val === "connected" || status.val === "connecting",
                onchange: (e: Event) => (selectedFps.val = parseInt((e.target as HTMLSelectElement).value, 10))
              },
              option({ value: "120", selected: selectedFps.val === 120 }, "120 fps (極限低遅延・対応端末)"),
              option({ value: "60", selected: selectedFps.val === 60 }, "60 fps (超低遅延・高滑らか)"),
              option({ value: "30", selected: selectedFps.val === 30 }, "30 fps (標準)"),
              option({ value: "24", selected: selectedFps.val === 24 }, "24 fps (映画風)"),
              option({ value: "15", selected: selectedFps.val === 15 }, "15 fps (省負荷)")
            )
          )
        ),
        div(
          { class: "form-group" },
          label({ class: "label" }, "ビットレート (ブロックノイズ低減)"),
          select(
            {
              class: "select",
              onchange: (e: Event) => {
                const b = parseInt((e.target as HTMLSelectElement).value, 10);
                selectedBitrate.val = b;
                if (status.val === "connected") {
                  rtc.applyBitrateParameters(b, selectedFps.val);
                }
              }
            },
            option({ value: "6000000", selected: selectedBitrate.val === 6000000 }, "6.0 Mbps (超高画質・ノイズ極小)"),
            option({ value: "4000000", selected: selectedBitrate.val === 4000000 }, "4.0 Mbps (高画質・推奨)"),
            option({ value: "2500000", selected: selectedBitrate.val === 2500000 }, "2.5 Mbps (標準)"),
            option({ value: "1500000", selected: selectedBitrate.val === 1500000 }, "1.5 Mbps (省帯域)"),
            option({ value: "800000", selected: selectedBitrate.val === 800000 }, "800 kbps (低負荷)")
          )
        )
      ),

      // Main Start / Stop Button
      () =>
        status.val === "connected" || status.val === "connecting" || status.val === "requesting_media"
          ? button({ class: "button button-danger", onclick: handleStop }, "送信停止")
          : button(
              {
                class: "button button-primary",
                disabled: () => !sessionId.val,
                onclick: handleStart
              },
              "送信開始"
            )
    ),

    () =>
      details(
        { class: "diagnostics-details", open: openDiagnostics.val },
        summary({ class: "diagnostics-summary" }, "接続診断情報 (リアルタイムログ)"),
        div(
          { style: "margin: 8px 0;" },
          button(
            {
              class: "button button-secondary",
              style: "font-size: 13px; padding: 4px 10px;",
              onclick: handleCopyLogs
            },
            () => copyStatus.val
          )
        ),
        pre({ class: "diagnostics-content" }, () => diagnostics.val)
      )
  );
}

van.add(document.getElementById("app")!, App());
