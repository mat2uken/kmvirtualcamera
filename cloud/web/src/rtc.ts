export async function waitForIceGatheringComplete(
  pc: RTCPeerConnection,
  timeoutMs = 15000,
  signal?: AbortSignal
): Promise<void> {
  if (pc.iceGatheringState === "complete") return;

  return new Promise<void>((resolve, reject) => {
    let timer: number | null = null;

    const cleanup = () => {
      if (timer !== null) clearTimeout(timer);
      pc.removeEventListener("icegatheringstatechange", onChange);
      signal?.removeEventListener("abort", onAbort);
    };

    const onChange = () => {
      if (pc.iceGatheringState === "complete") {
        cleanup();
        resolve();
      }
    };

    const onAbort = () => {
      cleanup();
      reject(new DOMException("ICE gathering aborted", "AbortError"));
    };

    timer = window.setTimeout(() => {
      cleanup();
      // Resolve anyway after timeout to proceed with gathered candidates
      resolve();
    }, timeoutMs);

    pc.addEventListener("icegatheringstatechange", onChange);
    signal?.addEventListener("abort", onAbort, { once: true });
  });
}

export class WebRtcSender {
  private pc: RTCPeerConnection | null = null;
  private localStream: MediaStream | null = null;
  private statsTimer: number | null = null;

  async getMedia(videoDeviceId?: string, audioDeviceId?: string): Promise<MediaStream> {
    this.stopMedia();

    const constraints: MediaStreamConstraints = {
      video: {
        deviceId: videoDeviceId ? { exact: videoDeviceId } : undefined,
        width: { ideal: 1280 },
        height: { ideal: 720 },
        frameRate: { ideal: 30, max: 30 },
        facingMode: { ideal: "environment" }
      },
      audio: audioDeviceId ? { deviceId: { exact: audioDeviceId } } : true
    };

    this.localStream = await navigator.mediaDevices.getUserMedia(constraints);
    return this.localStream;
  }

  async createPeerConnection(
    rtcConfig: RTCConfiguration,
    onStateChange: (state: RTCPeerConnectionState) => void,
    onStatsUpdate?: (stats: Record<string, unknown>) => void
  ): Promise<string> {
    if (!this.localStream) {
      throw new Error("No media stream available. Call getMedia() first.");
    }

    this.pc = new RTCPeerConnection(rtcConfig);

    this.pc.onconnectionstatechange = () => {
      if (this.pc) {
        onStateChange(this.pc.connectionState);
      }
    };

    for (const track of this.localStream.getTracks()) {
      this.pc.addTransceiver(track, {
        direction: "sendonly",
        streams: [this.localStream]
      });
    }

    const offer = await this.pc.createOffer();
    await this.pc.setLocalDescription(offer);

    await waitForIceGatheringComplete(this.pc, 15000);

    const localSdp = this.pc.localDescription?.sdp;
    if (!localSdp) {
      throw new Error("Failed to obtain candidate-complete local description.");
    }

    if (onStatsUpdate) {
      this.statsTimer = window.setInterval(async () => {
        if (this.pc && this.pc.connectionState === "connected") {
          const statsReport = await this.pc.getStats();
          const summary: Record<string, unknown> = {};
          statsReport.forEach((report) => {
            if (report.type === "outbound-rtp" && report.kind === "video") {
              summary.videoFps = report.framesPerSecond;
              summary.videoBytesSent = report.bytesSent;
              summary.videoWidth = report.frameWidth;
              summary.videoHeight = report.frameHeight;
            }
          });
          onStatsUpdate(summary);
        }
      }, 2000);
    }

    return localSdp;
  }

  async setAnswer(sdp: string): Promise<void> {
    if (!this.pc) {
      throw new Error("PeerConnection not initialized");
    }
    await this.pc.setRemoteDescription({ type: "answer", sdp });
  }

  stopMedia(): void {
    if (this.localStream) {
      this.localStream.getTracks().forEach((t) => t.stop());
      this.localStream = null;
    }
  }

  stop(): void {
    if (this.statsTimer !== null) {
      clearInterval(this.statsTimer);
      this.statsTimer = null;
    }
    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }
    this.stopMedia();
  }
}
