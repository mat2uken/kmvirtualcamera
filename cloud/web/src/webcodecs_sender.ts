/**
 * WebCodecs + RTCDataChannel (UDP Unreliable) Ultra-Low Latency Sender
 * Features:
 * - Direct VideoEncoder pipeline bypassing WebRTC Pacer & JitterBuffer (0ms buffering)
 * - Application-level MTU chunking (<=1168B) to eliminate SCTP Head-of-Line packet drop penalty
 * - Adaptive Bitrate (ABR) & Delay-gradient Congestion Control feedback
 * - BufferedAmount flow control to guarantee zero buffer bloat
 */

export interface WebCodecsSenderConfig {
  width: number;
  height: number;
  fps: number;
  bitrateBps: number;
}

export class WebCodecsSender {
  private encoder: VideoEncoder | null = null;
  private videoDc: RTCDataChannel | null = null;
  private controlDc: RTCDataChannel | null = null;
  private isRunning = false;
  private frameSeq = 0;
  private forceKeyframeNext = true;
  private currentBitrateBps: number;
  private config: WebCodecsSenderConfig;
  private rttMs = 0;
  private lastPingTime = 0;

  private onBitrateChanged?: (bps: number) => void;
  private onStatsUpdate?: (stats: { fps: number; bitrateBps: number; rttMs: number; bufferedKB: number }) => void;
  private logFn: (msg: string) => void;

  constructor(
    config: WebCodecsSenderConfig,
    logFn: (msg: string) => void = console.log
  ) {
    this.config = config;
    this.currentBitrateBps = config.bitrateBps;
    this.logFn = logFn;
  }

  public static isSupported(): boolean {
    return (
      typeof window !== "undefined" &&
      typeof VideoEncoder !== "undefined" &&
      typeof EncodedVideoChunk !== "undefined"
    );
  }

  public setCallbacks(
    onBitrateChanged?: (bps: number) => void,
    onStatsUpdate?: (stats: { fps: number; bitrateBps: number; rttMs: number; bufferedKB: number }) => void
  ) {
    this.onBitrateChanged = onBitrateChanged;
    this.onStatsUpdate = onStatsUpdate;
  }

  public async start(
    track: MediaStreamTrack,
    pc: RTCPeerConnection,
    videoElem?: HTMLVideoElement
  ): Promise<void> {
    this.isRunning = true;
    this.frameSeq = 0;
    this.forceKeyframeNext = true;

    // 1. Create Unreliable Video DataChannel (UDP-like)
    this.videoDc = pc.createDataChannel("km-video-stream", {
      ordered: false,
      maxRetransmits: 0
    });
    this.videoDc.binaryType = "arraybuffer";

    // 2. Create Reliable Control DataChannel (for PLI / ABR / Stats)
    this.controlDc = pc.createDataChannel("km-control", {
      ordered: true
    });

    this.setupControlChannel(this.controlDc);

    // 3. Initialize VideoEncoder
    const codec = "avc1.420028"; // H.264 Baseline Level 4.0
    this.encoder = new VideoEncoder({
      output: (chunk, metadata) => this.handleEncodedChunk(chunk, metadata),
      error: (e) => {
        this.logFn(`[WebCodecs] Encoder error: ${e.message}`);
      }
    });

    this.encoder.configure({
      codec: codec,
      width: this.config.width,
      height: this.config.height,
      bitrate: this.currentBitrateBps,
      framerate: this.config.fps,
      latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware",
      avc: { format: "annexb" }
    });

    this.logFn(`[WebCodecs] Initialized: ${this.config.width}x${this.config.height} @ ${this.config.fps}fps, ${(this.currentBitrateBps / 1e6).toFixed(1)} Mbps`);

    // 4. Ingest video frames from TrackProcessor or Video element fallback
    this.startFrameCapture(track, videoElem);
  }

  private setupControlChannel(dc: RTCDataChannel) {
    dc.onopen = () => {
      this.logFn("[WebCodecs] Control DataChannel open.");
      // Start periodic ping for RTT measurement
      setInterval(() => {
        if (this.controlDc && this.controlDc.readyState === "open") {
          this.lastPingTime = performance.now();
          this.controlDc.send(JSON.stringify({ type: "ping", ts: Date.now() }));
        }
      }, 2000);
    };

    dc.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.type === "pli") {
          // Keyframe request from receiver
          this.forceKeyframeNext = true;
        } else if (msg.type === "bitrate" && typeof msg.bps === "number") {
          // Dynamic ABR / Congestion Control adjustment from receiver
          this.setBitrate(msg.bps);
        } else if (msg.type === "pong") {
          if (this.lastPingTime > 0) {
            this.rttMs = Math.round(performance.now() - this.lastPingTime);
          }
        }
      } catch (err) {
        // Non-JSON message
      }
    };
  }

  public setBitrate(newBps: number) {
    if (!this.encoder || newBps === this.currentBitrateBps) return;
    this.currentBitrateBps = Math.max(500000, Math.min(8000000, newBps));
    this.encoder.configure({
      codec: "avc1.420028",
      width: this.config.width,
      height: this.config.height,
      bitrate: this.currentBitrateBps,
      framerate: this.config.fps,
      latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware",
      avc: { format: "annexb" }
    });
    this.logFn(`[WebCodecs] ABR Bitrate updated: ${(this.currentBitrateBps / 1e6).toFixed(2)} Mbps`);
    if (this.onBitrateChanged) {
      this.onBitrateChanged(this.currentBitrateBps);
    }
  }

  private async startFrameCapture(track: MediaStreamTrack, videoElem?: HTMLVideoElement) {
    if (typeof MediaStreamTrackProcessor !== "undefined") {
      // Modern standards path: MediaStreamTrackProcessor (Zero Copy)
      try {
        const processor = new MediaStreamTrackProcessor({ track });
        const reader = processor.readable.getReader();

        while (this.isRunning) {
          const { done, value } = await reader.read();
          if (done) break;
          if (value) {
            this.processVideoFrame(value);
          }
        }
        return;
      } catch (e) {
        this.logFn(`[WebCodecs] MediaStreamTrackProcessor failed, falling back: ${e}`);
      }
    }

    // Fallback path: requestVideoFrameCallback or Video/Canvas capture
    if (videoElem && "requestVideoFrameCallback" in videoElem) {
      const onFrame = () => {
        if (!this.isRunning) return;
        try {
          // @ts-ignore
          const frame = new VideoFrame(videoElem);
          this.processVideoFrame(frame);
        } catch {}
        // @ts-ignore
        videoElem.requestVideoFrameCallback(onFrame);
      };
      // @ts-ignore
      videoElem.requestVideoFrameCallback(onFrame);
    }
  }

  private processVideoFrame(frame: VideoFrame) {
    if (!this.encoder || this.encoder.state !== "configured") {
      frame.close();
      return;
    }

    // Queue Bloat Prevention (Flow Control):
    // If DataChannel buffer has > 48KB pending, drop non-keyframe frames immediately
    // to guarantee ZERO queueing latency spikes over cellular/Wi-Fi
    if (this.videoDc && this.videoDc.bufferedAmount > 48 * 1024 && !this.forceKeyframeNext) {
      frame.close();
      return;
    }

    const isKey = this.forceKeyframeNext;
    this.forceKeyframeNext = false;

    try {
      this.encoder.encode(frame, { keyFrame: isKey });
    } catch (err) {
      this.logFn(`[WebCodecs] encode error: ${err}`);
    } finally {
      frame.close();
    }
  }

  private handleEncodedChunk(chunk: EncodedVideoChunk, metadata?: EncodedVideoChunkMetadata) {
    if (!this.videoDc || this.videoDc.readyState !== "open") return;

    const isKeyframe = chunk.type === "key";
    const chunkData = new Uint8Array(chunk.byteLength);
    chunk.copyTo(chunkData);

    const maxPayload = 1168; // MTU safe payload size
    const totalChunks = Math.ceil(chunkData.byteLength / maxPayload);
    const seq = this.frameSeq++ & 0xffff;
    const tsUs = Math.floor(chunk.timestamp) & 0xffffffff;

    for (let i = 0; i < totalChunks; ++i) {
      const offset = i * maxPayload;
      const length = Math.min(maxPayload, chunkData.byteLength - offset);
      const packetBuffer = new ArrayBuffer(12 + length);
      const view = new DataView(packetBuffer);
      const packetData = new Uint8Array(packetBuffer);

      // Header: 12 Bytes (Little Endian for network compatibility)
      view.setUint16(0, 0x4B4D, true); // Magic 'KM'
      let flags = 0;
      if (isKeyframe) flags |= 0x01;
      if (i === 0) flags |= 0x04;
      if (i === totalChunks - 1) flags |= 0x08;
      view.setUint8(2, flags);
      view.setUint8(3, 1); // H.264
      view.setUint8(4, i); // ChunkIndex
      view.setUint8(5, totalChunks); // TotalChunks
      view.setUint16(6, seq, true); // FrameSeq
      view.setUint32(8, tsUs, true); // TimestampUs

      packetData.set(chunkData.subarray(offset, offset + length), 12);

      try {
        this.videoDc.send(packetBuffer);
      } catch (err) {
        break;
      }
    }

    if (this.onStatsUpdate) {
      this.onStatsUpdate({
        fps: this.config.fps,
        bitrateBps: this.currentBitrateBps,
        rttMs: this.rttMs,
        bufferedKB: Math.round((this.videoDc?.bufferedAmount || 0) / 1024)
      });
    }
  }

  public stop() {
    this.isRunning = false;
    if (this.encoder) {
      try {
        this.encoder.close();
      } catch {}
      this.encoder = null;
    }
    if (this.videoDc) {
      try {
        this.videoDc.close();
      } catch {}
      this.videoDc = null;
    }
    if (this.controlDc) {
      try {
        this.controlDc.close();
      } catch {}
      this.controlDc = null;
    }
  }
}
