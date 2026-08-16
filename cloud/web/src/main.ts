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

  const parsed = parseFragment();
  const sessionId = van.state<string>(parsed?.sessionId || "");
  const joinToken = van.state<string>(parsed?.joinToken || "");

  const api = new SignalingClient();
  const rtc = new WebRtcSender();
  const videoElem = video({ class: "preview-video", playsinline: true, muted: true, autoplay: true });

  const updateDevices = async () => {
    try {
      const devices = await navigator.mediaDevices.enumerateDevices();
      const v = devices.filter((d) => d.kind === "videoinput").map((d, i) => ({ id: d.deviceId, label: d.label || `カメラ ${i + 1}` }));
      const a = devices.filter((d) => d.kind === "audioinput").map((d, i) => ({ id: d.deviceId, label: d.label || `マイク ${i + 1}` }));
      videoDevices.val = v;
      audioDevices.val = a;
      if (v.length && !selectedVideo.val) selectedVideo.val = v[0].id;
      if (a.length && !selectedAudio.val) selectedAudio.val = a[0].id;
    } catch {
      // Ignore if permission not granted yet
    }
  };

  updateDevices();

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

      const stream = await rtc.getMedia(selectedVideo.val, selectedAudio.val);
      (videoElem as HTMLVideoElement).srcObject = stream;
      await updateDevices();

      status.val = "connecting";
      statusText.val = "セッション登録中...";

      let nonce = sessionStorage.getItem("wrtc.nonce");
      if (!nonce) {
        nonce = crypto.randomUUID();
        sessionStorage.setItem("wrtc.nonce", nonce);
      }

      const claimRes = await api.claim(sessionId.val, joinToken.val, nonce);
      diagnostics.val = `Claimed session ${sessionId.val.slice(0, 8)}... Expiry: ${claimRes.expiresAt}`;

      statusText.val = "Offer SDP生成・ICE収集中...";
      const offerSdp = await rtc.createPeerConnection(
        claimRes.rtcConfiguration,
        (pcState) => {
          diagnostics.val += `\nPeerConnection state: ${pcState}`;
          if (pcState === "connected") {
            status.val = "connected";
            statusText.val = "接続中 (送信中)";
          } else if (pcState === "failed" || pcState === "disconnected") {
            status.val = "error";
            errorMessage.val = `WebRTC接続が切断されました (${pcState})。`;
          }
        },
        (stats) => {
          diagnostics.val = `Connected.\nStats: ${JSON.stringify(stats, null, 2)}`;
        }
      );

      statusText.val = "Offer送信中...";
      await api.putOffer(sessionId.val, claimRes.senderToken, offerSdp);

      statusText.val = "Windows側Answer待機中 (ポーリング)...";
      const answerSdp = await api.pollAnswer(sessionId.val, claimRes.senderToken, claimRes.poll);

      statusText.val = "接続確立中...";
      await rtc.setAnswer(answerSdp);
    } catch (err: unknown) {
      status.val = "error";
      const msg = err instanceof Error ? err.message : String(err);
      errorMessage.val = msg;
      diagnostics.val += `\nError: ${msg}`;
      rtc.stop();
    }
  };

  const handleStop = async () => {
    rtc.stop();
    (videoElem as HTMLVideoElement).srcObject = null;
    status.val = "idle";
    statusText.val = "停止";
    diagnostics.val += "\nStopped.";
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
        status.val === "idle" || status.val === "error"
          ? div({ class: "video-placeholder" }, "「送信開始」を押すとカメラ映像が表示されます")
          : div()
    ),

    () =>
      errorMessage.val
        ? div({ class: "message-box message-error" }, errorMessage.val)
        : div(),

    div(
      { class: "controls" },
      div(
        { class: "form-group" },
        label({ class: "label" }, "カメラ選択"),
        select(
          {
            class: "select",
            disabled: () => status.val === "connected" || status.val === "connecting",
            onchange: (e: Event) => (selectedVideo.val = (e.target as HTMLSelectElement).value)
          },
          () => videoDevices.val.map((d) => option({ value: d.id, selected: d.id === selectedVideo.val }, d.label))
        )
      ),

      div(
        { class: "form-group" },
        label({ class: "label" }, "マイク選択"),
        select(
          {
            class: "select",
            disabled: () => status.val === "connected" || status.val === "connecting",
            onchange: (e: Event) => (selectedAudio.val = (e.target as HTMLSelectElement).value)
          },
          () => audioDevices.val.map((d) => option({ value: d.id, selected: d.id === selectedAudio.val }, d.label))
        )
      ),

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

    details(
      { class: "diagnostics-details" },
      summary({ class: "diagnostics-summary" }, "接続診断情報"),
      pre({ class: "diagnostics-content" }, () => diagnostics.val)
    )
  );
}

van.add(document.getElementById("app")!, App());
