export async function waitForIceGatheringComplete(
  pc: RTCPeerConnection,
  timeoutMs = 6000,
  onCandidateLogged?: (cand: string) => void,
  signal?: AbortSignal
): Promise<string[]> {
  const candidates: string[] = [];
  if (pc.iceGatheringState === "complete") return candidates;

  return new Promise<string[]>((resolve, reject) => {
    let timer: number | null = null;

    const cleanup = () => {
      if (timer !== null) clearTimeout(timer);
      pc.removeEventListener("icegatheringstatechange", onChange);
      pc.removeEventListener("icecandidate", onCandidate);
      signal?.removeEventListener("abort", onAbort);
    };

    const onChange = () => {
      if (pc.iceGatheringState === "complete") {
        cleanup();
        resolve(candidates);
      }
    };

    const onCandidate = (event: RTCPeerConnectionIceEvent) => {
      if (event.candidate) {
        const cStr = event.candidate.candidate;
        candidates.push(cStr);
        onCandidateLogged?.(`Candidate [${event.candidate.type || "unknown"}] ${cStr}`);
      } else {
        cleanup();
        resolve(candidates);
      }
    };

    const onAbort = () => {
      cleanup();
      reject(new DOMException("ICE gathering aborted", "AbortError"));
    };

    timer = window.setTimeout(() => {
      cleanup();
      resolve(candidates);
    }, timeoutMs);

    pc.addEventListener("icegatheringstatechange", onChange);
    pc.addEventListener("icecandidate", onCandidate);
    signal?.addEventListener("abort", onAbort, { once: true });
  });
}

function enhanceSdpForLowLatency(sdp: string, bitrateBps = 2_500_000): string {
  const lines = sdp.split("\r\n");
  const result: string[] = [];
  let inVideo = false;

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    if (line.startsWith("m=video")) {
      inVideo = true;
      result.push(line);
      result.push(`b=AS:${Math.round(bitrateBps / 1000)}`);
      result.push(`b=TIAS:${bitrateBps}`);
      continue;
    } else if (line.startsWith("m=audio") || line.startsWith("m=application")) {
      inVideo = false;
    }

    if (inVideo && line.startsWith("a=rtpmap:") && line.includes("H264/90000")) {
      const pt = line.split(" ")[0].substring(9);
      result.push(line);
      result.push(`a=rtcp-fb:${pt} transport-cc`);
      result.push(`a=rtcp-fb:${pt} goog-remb`);
      result.push(`a=rtcp-fb:${pt} ccm fir`);
      result.push(`a=rtcp-fb:${pt} nack`);
      result.push(`a=rtcp-fb:${pt} nack pli`);
      continue;
    }

    if (inVideo && line.startsWith("a=fmtp:") && line.includes("packetization-mode=1")) {
      // Enhance H.264 fmtp with multi-slice, max macroblock throughput, and recommended NALU size
      let enhancedFmtp = line;
      if (!enhancedFmtp.includes("level-asymmetry-allowed")) {
        enhancedFmtp += ";level-asymmetry-allowed=1";
      }
      if (!enhancedFmtp.includes("max-mbps")) {
        enhancedFmtp += ";max-mbps=245760;max-fs=8160;max-smbps=245760";
      }
      if (!enhancedFmtp.includes("max-rcmd-nalu-size")) {
        enhancedFmtp += ";max-rcmd-nalu-size=1200";
      }
      result.push(enhancedFmtp);
      continue;
    }

    result.push(line);
  }
  return result.join("\r\n");
}

function buildVideoConstraint(
  deviceIdOrFacing?: string,
  width = 1280,
  height = 720,
  frameRate = 30
): MediaTrackConstraints {
  const constraint: MediaTrackConstraints = {
    width: { ideal: width },
    height: { ideal: height },
    frameRate: { ideal: frameRate, max: frameRate }
  };

  if (deviceIdOrFacing === "user" || deviceIdOrFacing === "environment") {
    constraint.facingMode = { ideal: deviceIdOrFacing };
  } else if (deviceIdOrFacing) {
    constraint.deviceId = { ideal: deviceIdOrFacing };
  } else {
    constraint.facingMode = { ideal: "environment" };
  }
  return constraint;
}

export class WebRtcSender {
  private pc: RTCPeerConnection | null = null;
  private localStream: MediaStream | null = null;
  private statsTimer: number | null = null;
  private keyframeTimer: number | null = null;

  private startPeriodicKeyframe(intervalMs = 2000): void {
    if (this.keyframeTimer !== null) {
      clearInterval(this.keyframeTimer);
    }
    this.keyframeTimer = window.setInterval(() => {
      if (this.pc && this.pc.connectionState === "connected") {
        this.generateKeyFrame();
      }
    }, intervalMs);
  }

  generateKeyFrame(): void {
    if (!this.pc) return;
    for (const sender of this.pc.getSenders()) {
      if (sender.track && sender.track.kind === "video") {
        // @ts-expect-error generateKeyFrame is supported in modern Chromium WebRTC
        if (typeof sender.generateKeyFrame === "function") {
          try {
            // @ts-expect-error generateKeyFrame
            sender.generateKeyFrame();
          } catch {
            // Ignore if unsupported
          }
        }
      }
    }
  }

  async getMedia(
    videoDeviceIdOrFacing?: string,
    audioDeviceId?: string,
    width = 1280,
    height = 720,
    frameRate = 30
  ): Promise<MediaStream> {
    this.stopMedia();

    const videoConstraint = buildVideoConstraint(videoDeviceIdOrFacing, width, height, frameRate);
    // Disable WebRTC software DSP buffer to eliminate 20-40ms audio buffering delay
    const audioConstraint: boolean | MediaTrackConstraints = audioDeviceId
      ? {
          deviceId: { ideal: audioDeviceId },
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false
        }
      : {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false
        };

    try {
      this.localStream = await navigator.mediaDevices.getUserMedia({
        video: videoConstraint,
        audio: audioConstraint
      });
    } catch (err) {
      console.warn("getMedia failed with requested constraints, trying fallback:", err);
      try {
        this.localStream = await navigator.mediaDevices.getUserMedia({
          video: { facingMode: { ideal: "environment" } },
          audio: true
        });
      } catch {
        this.localStream = await navigator.mediaDevices.getUserMedia({
          video: true,
          audio: true
        });
      }
    }

    for (const track of this.localStream.getVideoTracks()) {
      if ("contentHint" in track) {
        track.contentHint = "motion";
      }
    }

    return this.localStream;
  }

  async switchVideo(
    newVideoDeviceIdOrFacing?: string,
    width = 1280,
    height = 720,
    frameRate = 30
  ): Promise<MediaStream> {
    const videoConstraint = buildVideoConstraint(newVideoDeviceIdOrFacing, width, height, frameRate);

    // CRITICAL: Stop previous video tracks first so hardware release allows the new camera to open
    const oldVideoTracks = this.localStream ? this.localStream.getVideoTracks() : [];
    oldVideoTracks.forEach((t) => t.stop());

    let newStream: MediaStream;
    try {
      newStream = await navigator.mediaDevices.getUserMedia({
        video: videoConstraint,
        audio: false
      });
    } catch (err) {
      console.warn("switchVideo failed with exact constraints, trying fallback:", err);
      try {
        newStream = await navigator.mediaDevices.getUserMedia({
          video: newVideoDeviceIdOrFacing ? { deviceId: { ideal: newVideoDeviceIdOrFacing } } : true,
          audio: false
        });
      } catch {
        newStream = await navigator.mediaDevices.getUserMedia({
          video: true,
          audio: false
        });
      }
    }

    const newVideoTrack = newStream.getVideoTracks()[0];
    if (newVideoTrack && "contentHint" in newVideoTrack) {
      newVideoTrack.contentHint = "motion";
    }

    if (!this.localStream) {
      this.localStream = new MediaStream();
    }

    // Remove old video tracks from localStream and add new video track
    oldVideoTracks.forEach((t) => this.localStream?.removeTrack(t));
    if (newVideoTrack) {
      this.localStream.addTrack(newVideoTrack);
    }

    // Replace track on RTCPeerConnection video sender
    if (this.pc && newVideoTrack) {
      const senders = this.pc.getSenders();
      const videoSender = senders.find(
        (s) => s.track?.kind === "video" || (!s.track && (s as unknown as { kind?: string }).kind === "video")
      ) || senders[0];
      if (videoSender) {
        await videoSender.replaceTrack(newVideoTrack);
      }
    }

    return this.localStream;
  }

  async switchAudio(newAudioDeviceId?: string): Promise<MediaStream> {
    const audioConstraint: boolean | MediaTrackConstraints = newAudioDeviceId
      ? { deviceId: { ideal: newAudioDeviceId } }
      : true;

    // Stop current audio tracks first
    const oldAudioTracks = this.localStream ? this.localStream.getAudioTracks() : [];
    oldAudioTracks.forEach((t) => t.stop());

    let newStream: MediaStream;
    try {
      newStream = await navigator.mediaDevices.getUserMedia({
        video: false,
        audio: audioConstraint
      });
    } catch (err) {
      console.warn("switchAudio failed with requested device, falling back to default:", err);
      newStream = await navigator.mediaDevices.getUserMedia({
        video: false,
        audio: true
      });
    }

    const newAudioTrack = newStream.getAudioTracks()[0];

    if (!this.localStream) {
      this.localStream = new MediaStream();
    }

    // Remove old audio tracks from localStream and add new audio track
    oldAudioTracks.forEach((t) => this.localStream?.removeTrack(t));
    if (newAudioTrack) {
      this.localStream.addTrack(newAudioTrack);
    }

    // Replace track on RTCPeerConnection audio sender
    if (this.pc && newAudioTrack) {
      const senders = this.pc.getSenders();
      const audioSender = senders.find(
        (s) => s.track?.kind === "audio" || (!s.track && (s as unknown as { kind?: string }).kind === "audio")
      ) || senders[1];
      if (audioSender) {
        await audioSender.replaceTrack(newAudioTrack);
      }
    }

    return this.localStream;
  }

  async switchMedia(
    newVideoDeviceId?: string,
    newAudioDeviceId?: string,
    width = 1280,
    height = 720,
    frameRate = 30
  ): Promise<MediaStream> {
    if (newVideoDeviceId !== undefined) {
      await this.switchVideo(newVideoDeviceId, width, height, frameRate);
    }
    if (newAudioDeviceId !== undefined) {
      await this.switchAudio(newAudioDeviceId);
    }
    return this.localStream || new MediaStream();
  }

  getActiveVideoTrackSettings(): MediaTrackSettings | null {
    if (this.localStream) {
      const vTrack = this.localStream.getVideoTracks()[0];
      if (vTrack) return vTrack.getSettings();
    }
    return null;
  }

  getActiveAudioTrackSettings(): MediaTrackSettings | null {
    if (this.localStream) {
      const aTrack = this.localStream.getAudioTracks()[0];
      if (aTrack) return aTrack.getSettings();
    }
    return null;
  }

  async applyBitrateParameters(targetBitrateBps = 2_500_000, targetFps = 30): Promise<void> {
    if (!this.pc) return;
    const senders = this.pc.getSenders();
    for (const sender of senders) {
      if (sender.track && sender.track.kind === "video") {
        try {
          const params = sender.getParameters();
          if (!params.encodings || params.encodings.length === 0) {
            params.encodings = [{}];
          }
          for (const enc of params.encodings) {
            enc.maxBitrate = targetBitrateBps;
            enc.maxFramerate = targetFps;
            enc.scaleResolutionDownBy = 1.0;
            // @ts-expect-error networkPriority extension
            enc.networkPriority = "high";
            // @ts-expect-error priority extension
            enc.priority = "high";
          }
          // @ts-expect-error degradationPreference extension
          params.degradationPreference = "maintain-framerate";
          await sender.setParameters(params);
        } catch {
          // Ignore if browser does not support setting parameters
        }
      }
    }
  }

  async createPeerConnection(
    rtcConfig: RTCConfiguration,
    onStateChange: (state: RTCPeerConnectionState) => void,
    onStatsUpdate?: (stats: Record<string, unknown>) => void,
    targetBitrateBps = 2_500_000,
    targetFps = 30,
    onIceStateChange?: (state: RTCIceConnectionState) => void,
    onDiagnosticLog?: (msg: string) => void
  ): Promise<string> {
    if (!this.localStream) {
      throw new Error("No media stream available. Call getMedia() first.");
    }

    this.pc = new RTCPeerConnection(rtcConfig);

    this.pc.onconnectionstatechange = () => {
      if (this.pc) {
        onDiagnosticLog?.(`ConnectionState: ${this.pc.connectionState}`);
        onStateChange(this.pc.connectionState);
        if (this.pc.connectionState === "connected") {
          this.applyBitrateParameters(targetBitrateBps, targetFps);
          this.startPeriodicKeyframe(2000);
        }
      }
    };

    this.pc.oniceconnectionstatechange = () => {
      if (this.pc) {
        onDiagnosticLog?.(`ICEConnectionState: ${this.pc.iceConnectionState}`);
        if (onIceStateChange) {
          onIceStateChange(this.pc.iceConnectionState);
        }
      }
    };

    this.pc.onicecandidateerror = (event) => {
      // @ts-expect-error RTCIceCandidateErrorEvent properties
      onDiagnosticLog?.(`ICE Candidate Error: ${event.errorCode} ${event.errorText} (${event.url})`);
    };

    for (const track of this.localStream.getTracks()) {
      const transceiver = this.pc.addTransceiver(track, {
        direction: "sendonly",
        streams: [this.localStream]
      });

      if (track.kind === "video" && typeof transceiver.setCodecPreferences === "function" && typeof RTCRtpSender.getCapabilities === "function") {
        const capabilities = RTCRtpSender.getCapabilities("video");
        if (capabilities && capabilities.codecs) {
          const h264Codecs = capabilities.codecs.filter((c) => c.mimeType.toLowerCase() === "video/h264");
          // Prioritize Baseline / Constrained Baseline profile for zero-latency, B-frame-free VideoToolbox encoding
          h264Codecs.sort((a, b) => {
            const aFmt = (a.sdpFmtpLine || "").toLowerCase();
            const bFmt = (b.sdpFmtpLine || "").toLowerCase();
            const aIsBaseline = aFmt.includes("42e0") || aFmt.includes("4200");
            const bIsBaseline = bFmt.includes("42e0") || bFmt.includes("4200");
            if (aIsBaseline && !bIsBaseline) return -1;
            if (!aIsBaseline && bIsBaseline) return 1;
            return 0;
          });
          const otherCodecs = capabilities.codecs.filter((c) => c.mimeType.toLowerCase() !== "video/h264");
          try {
            transceiver.setCodecPreferences([...h264Codecs, ...otherCodecs]);
          } catch {
            // Ignore if browser unsupported
          }
        }
      }
    }

    const offer = await this.pc.createOffer();
    await this.pc.setLocalDescription(offer);

    onDiagnosticLog?.("ICE候補の収集を開始...");
    const gathered = await waitForIceGatheringComplete(this.pc, 6000, onDiagnosticLog);
    onDiagnosticLog?.(`ICE収集完了: ${gathered.length} 個の候補を収集しました。`);

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
            if (report.type === "candidate-pair" && report.state === "succeeded") {
              summary.rtt = report.currentRoundTripTime;
            }
          });
          onStatsUpdate(summary);
        }
      }, 2000);
    }

    return enhanceSdpForLowLatency(localSdp, targetBitrateBps);
  }

  async setAnswer(sdp: string, onDiagnosticLog?: (msg: string) => void): Promise<void> {
    if (!this.pc) {
      throw new Error("PeerConnection not initialized");
    }
    onDiagnosticLog?.(`Answer SDPを適用中 (${sdp.length} 文字)...`);
    // RFC 4145 / RFC 8842: Answerer must use active or passive, never actpass
    const sanitizedSdp = sdp.replace(/a=setup:actpass/g, "a=setup:passive");
    await this.pc.setRemoteDescription({ type: "answer", sdp: sanitizedSdp });
    onDiagnosticLog?.("Answer SDPを適用完了。ICE接続検証中...");
  }

  stopMedia(): void {
    if (this.localStream) {
      this.localStream.getTracks().forEach((t) => t.stop());
      this.localStream = null;
    }
  }

  stop(): void {
    if (this.keyframeTimer !== null) {
      clearInterval(this.keyframeTimer);
      this.keyframeTimer = null;
    }
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
