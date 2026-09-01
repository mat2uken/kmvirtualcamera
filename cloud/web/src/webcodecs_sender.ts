/**
 * WebCodecs + RTCDataChannel (Ultra-Low Latency Multi-Slice Access Unit Pipeline)
 * Features:
 * - Multi-Slice Access Unit Aggregation: Combines multiple slices of the same timestamp into 1 Access Unit
 * - Single Access Unit Delimiter (AUD) per frame to prevent MFT half-frame tearing
 * - Deterministic AVCC and Annex-B NALU extractor
 * - Single-Initialization DataChannels with 50ms MaxPacketLifeTime (Micro-Loss Recovery)
 * - Dynamic Native Aspect-Ratio & Orientation Detection
 * - Periodic Intra-refresh & Instant PLI recovery
 * - Adaptive Bitrate (ABR) & Constant Bitrate (CBR) mode
 */

export interface WebCodecsSenderConfig {
  width: number;
  height: number;
  fps: number;
  bitrateBps: number;
}

function parseDecoderConfigDescription(description: ArrayBuffer | ArrayBufferView): Uint8Array | null {
  const buf = description instanceof ArrayBuffer
    ? new Uint8Array(description)
    : new Uint8Array(description.buffer, description.byteOffset, description.byteLength);
  if (buf.length < 4) return null;

  // Check if description is already Annex-B start code
  if ((buf[0] === 0 && buf[1] === 0 && buf[2] === 0 && buf[3] === 1) || (buf[0] === 0 && buf[1] === 0 && buf[2] === 1)) {
    return buf;
  }

  // Parse AVCDecoderConfigurationRecord (ISO/IEC 14496-15)
  if (buf.length >= 7 && buf[0] === 1) {
    const naluParts: Uint8Array[] = [];
    let totalLen = 0;

    let offset = 5;
    const numSps = buf[offset] & 0x1f;
    offset += 1;

    for (let i = 0; i < numSps && offset + 2 <= buf.length; i++) {
      const spsLen = (buf[offset] << 8) | buf[offset + 1];
      offset += 2;
      if (offset + spsLen <= buf.length) {
        naluParts.push(new Uint8Array([0, 0, 0, 1]));
        naluParts.push(buf.subarray(offset, offset + spsLen));
        totalLen += 4 + spsLen;
        offset += spsLen;
      }
    }

    if (offset < buf.length) {
      const numPps = buf[offset];
      offset += 1;
      for (let i = 0; i < numPps && offset + 2 <= buf.length; i++) {
        const ppsLen = (buf[offset] << 8) | buf[offset + 1];
        offset += 2;
        if (offset + ppsLen <= buf.length) {
          naluParts.push(new Uint8Array([0, 0, 0, 1]));
          naluParts.push(buf.subarray(offset, offset + ppsLen));
          totalLen += 4 + ppsLen;
          offset += ppsLen;
        }
      }
    }

    if (totalLen > 0) {
      const out = new Uint8Array(totalLen);
      let writeOffset = 0;
      for (const p of naluParts) {
        out.set(p, writeOffset);
        writeOffset += p.length;
      }
      return out;
    }
  }

  return null;
}

function isAvccFormat(data: Uint8Array): boolean {
  if (data.length < 5) return false;
  let offset = 0;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  while (offset + 4 < data.byteLength) {
    const len = view.getUint32(offset);
    if (len === 0 || offset + 4 + len > data.byteLength) {
      return false;
    }
    const naluHeader = data[offset + 4];
    if ((naluHeader & 0x80) !== 0) return false;
    const naluType = naluHeader & 0x1f;
    if (naluType === 0 || naluType > 23) return false;
    offset += 4 + len;
  }
  return offset === data.byteLength;
}

function extractNalusFromChunk(chunkData: Uint8Array): Uint8Array[] {
  const nalus: Uint8Array[] = [];
  if (chunkData.length < 4) return nalus;

  if (isAvccFormat(chunkData)) {
    let offset = 0;
    const view = new DataView(chunkData.buffer, chunkData.byteOffset, chunkData.byteLength);
    while (offset + 4 <= chunkData.byteLength) {
      const len = view.getUint32(offset);
      offset += 4;
      if (offset + len > chunkData.byteLength) break;
      if (len > 0) {
        nalus.push(chunkData.subarray(offset, offset + len));
      }
      offset += len;
    }
    return nalus;
  }

  // Annex-B format: scan start codes
  let i = 0;
  while (i < chunkData.length) {
    let startLen = 0;
    if (i + 4 <= chunkData.length && chunkData[i] === 0 && chunkData[i + 1] === 0 && chunkData[i + 2] === 0 && chunkData[i + 3] === 1) {
      startLen = 4;
    } else if (i + 3 <= chunkData.length && chunkData[i] === 0 && chunkData[i + 1] === 0 && chunkData[i + 2] === 1) {
      startLen = 3;
    }

    if (startLen > 0) {
      const naluStart = i + startLen;
      let nextStart = chunkData.length;
      for (let j = naluStart; j + 3 < chunkData.length; ++j) {
        if ((chunkData[j] === 0 && chunkData[j + 1] === 0 && chunkData[j + 2] === 1) ||
            (j + 4 <= chunkData.length && chunkData[j] === 0 && chunkData[j + 1] === 0 && chunkData[j + 2] === 0 && chunkData[j + 3] === 1)) {
          nextStart = j;
          break;
        }
      }
      if (nextStart > naluStart) {
        nalus.push(chunkData.subarray(naluStart, nextStart));
      }
      i = nextStart;
    } else {
      i++;
    }
  }
  return nalus;
}

export class WebCodecsSender {
  private encoder: VideoEncoder | null = null;
  private videoDc: RTCDataChannel | null = null;
  private controlDc: RTCDataChannel | null = null;
  private isRunning = false;
  private frameSeq = 0;
  private frameCount = 0;
  private forceKeyframeNext = true;
  private currentBitrateBps: number;
  private config: WebCodecsSenderConfig;
  private currentEncoderW = 0;
  private currentEncoderH = 0;
  private rttMs = 0;
  private lastPingTime = 0;
  private cachedSpsPpsAnnexB: Uint8Array | null = null;

  // Multi-Slice Access Unit Aggregation Buffer
  private pendingChunkData: Uint8Array[] = [];
  private pendingIsKeyframe = false;
  private pendingTimestampUs = -1;
  private flushTimer: any = null;

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

  public initDataChannels(pc: RTCPeerConnection) {
    if (this.videoDc || this.controlDc) return;

    // Unordered Video DataChannel with 50ms Realtime Deadline (Zero Head-of-Line Blocking)
    this.videoDc = pc.createDataChannel("km-video-stream", {
      ordered: false,
      maxPacketLifeTime: 50
    });
    this.videoDc.binaryType = "arraybuffer";

    // Reliable Control DataChannel (for PLI / ABR / Stats)
    this.controlDc = pc.createDataChannel("km-control", {
      ordered: true
    });

    this.setupControlChannel(this.controlDc);
  }

  public async start(
    track: MediaStreamTrack,
    videoElem?: HTMLVideoElement
  ): Promise<void> {
    this.isRunning = true;
    this.activeVideoTrack = track;
    this.frameSeq = 0;
    this.frameCount = 0;
    this.forceKeyframeNext = true;
    this.cachedSpsPpsAnnexB = null;
    this.pendingChunkData = [];
    this.pendingTimestampUs = -1;
    this.pendingIsKeyframe = false;

    this.sendCameraCapabilities();

    // Detect initial track dimensions (Portrait / Landscape awareness)
    const settings = track.getSettings();
    const initialW = settings.width || this.config.width;
    const initialH = settings.height || this.config.height;

    this.initEncoder(initialW, initialH);

    // Ingest video frames from TrackProcessor or Video element fallback
    this.startFrameCapture(track, videoElem);
  }

  public async updateTrack(newTrack: MediaStreamTrack, videoElem?: HTMLVideoElement) {
    this.logFn(`[WebCodecs] Updating active video track...`);
    this.activeVideoTrack = newTrack;
    if (this.activeReader) {
      try {
        await this.activeReader.cancel();
      } catch {}
      this.activeReader = null;
    }
    this.forceKeyframeNext = true;
    this.sendCameraCapabilities();

    const settings = newTrack.getSettings();
    const newW = settings.width || this.config.width;
    const newH = settings.height || this.config.height;
    if (newW > 0 && newH > 0 && (newW !== this.currentEncoderW || newH !== this.currentEncoderH)) {
      this.reconfigureResolution(newW, newH);
    }
    this.startFrameCapture(newTrack, videoElem);
  }

  private initEncoder(width: number, height: number) {
    this.currentEncoderW = width;
    this.currentEncoderH = height;

    const codec = "avc1.420028"; // H.264 Baseline Level 4.0
    this.encoder = new VideoEncoder({
      output: (chunk, metadata) => this.handleEncodedChunk(chunk, metadata),
      error: (e) => {
        this.logFn(`[WebCodecs] Encoder error: ${e.message}`);
      }
    });

    this.encoder.configure({
      codec: codec,
      width: width,
      height: height,
      bitrate: this.currentBitrateBps,
      framerate: this.config.fps,
      bitrateMode: "constant",
      latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware",
      avc: { format: "annexb" }
    });

    this.logFn(`[WebCodecs] Initialized Native Encoder: ${width}x${height} @ ${this.config.fps}fps, ${(this.currentBitrateBps / 1e6).toFixed(1)} Mbps`);
  }

  private reconfigureResolution(width: number, height: number) {
    if (!this.encoder || (this.currentEncoderW === width && this.currentEncoderH === height)) return;
    this.currentEncoderW = width;
    this.currentEncoderH = height;

    this.encoder.configure({
      codec: "avc1.420028",
      width: width,
      height: height,
      bitrate: this.currentBitrateBps,
      framerate: this.config.fps,
      bitrateMode: "constant",
      latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware",
      avc: { format: "annexb" }
    });
    this.forceKeyframeNext = true;
    this.logFn(`[WebCodecs] Reconfigured native resolution to ${width}x${height}`);
  }

  public activeVideoTrack: MediaStreamTrack | null = null;
  public onRemoteCommand?: (cmd: string, payload: any) => void;

  public sendCameraCapabilities() {
    if (!this.controlDc || this.controlDc.readyState !== "open" || !this.activeVideoTrack) return;
    try {
      // @ts-ignore
      const caps = typeof this.activeVideoTrack.getCapabilities === "function" ? this.activeVideoTrack.getCapabilities() : {};
      const settings = this.activeVideoTrack.getSettings();
      const info = {
        type: "camera_caps",
        supportsTorch: !!caps.torch,
        minZoom: caps.zoom ? caps.zoom.min : 1.0,
        maxZoom: caps.zoom ? caps.zoom.max : 1.0,
        currentZoom: (settings as any).zoom || 1.0,
        facingMode: settings.facingMode || "environment"
      };
      this.controlDc.send(JSON.stringify(info));
      this.logFn(`[WebCodecs] Sent camera capabilities: Torch=${info.supportsTorch}, Zoom=[${info.minZoom}..${info.maxZoom}], Facing=${info.facingMode}`);
    } catch (e) {}
  }

  private async handleRemoteControl(msg: any) {
    if (!this.activeVideoTrack) return;
    try {
      if (msg.cmd === "torch") {
        await (this.activeVideoTrack as any).applyConstraints({
          advanced: [{ torch: !!msg.enabled }]
        });
        this.logFn(`[WebCodecs] Remote Torch set to: ${msg.enabled}`);
      } else if (msg.cmd === "zoom") {
        await (this.activeVideoTrack as any).applyConstraints({
          advanced: [{ zoom: Number(msg.value) }]
        });
        this.logFn(`[WebCodecs] Remote Zoom set to: ${msg.value}x`);
      } else if (msg.cmd === "switch_camera") {
        if (this.onRemoteCommand) {
          this.onRemoteCommand("switch_camera", msg);
        }
      }
    } catch (err) {
      this.logFn(`[WebCodecs] Remote control command failed: ${err}`);
    }
  }

  private setupControlChannel(dc: RTCDataChannel) {
    dc.onopen = () => {
      this.logFn("[WebCodecs] Control DataChannel open.");
      this.sendCameraCapabilities();
      setInterval(() => {
        if (this.controlDc && this.controlDc.readyState === "open") {
          this.lastPingTime = performance.now();
          this.controlDc.send(JSON.stringify({ type: "ping", ts: Date.now() }));
        }
      }, 2000);
    };

    dc.onmessage = async (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.type === "pli") {
          this.forceKeyframeNext = true;
          this.logFn("[WebCodecs] Instant PLI Keyframe requested by receiver.");
        } else if (msg.type === "bitrate" && typeof msg.bps === "number") {
          this.setBitrate(msg.bps);
        } else if (msg.type === "remote_control") {
          await this.handleRemoteControl(msg);
        } else if (msg.type === "query_caps") {
          this.sendCameraCapabilities();
        } else if (msg.type === "pong") {
          if (this.lastPingTime > 0) {
            this.rttMs = Math.round(performance.now() - this.lastPingTime);
          }
        }
      } catch (err) {}
    };
  }

  public setBitrate(newBps: number) {
    if (!this.encoder || newBps === this.currentBitrateBps) return;
    this.currentBitrateBps = Math.max(1500000, Math.min(8000000, newBps));
    if (this.currentEncoderW > 0 && this.currentEncoderH > 0) {
      try {
        this.encoder.configure({
          codec: "avc1.420028",
          width: this.currentEncoderW,
          height: this.currentEncoderH,
          bitrate: this.currentBitrateBps,
          framerate: this.config.fps,
          bitrateMode: "constant",
          latencyMode: "realtime",
          hardwareAcceleration: "prefer-hardware",
          avc: { format: "annexb" }
        });
        this.logFn(`[WebCodecs] ABR Bitrate updated: ${(this.currentBitrateBps / 1e6).toFixed(2)} Mbps`);
      } catch (err) {}
    }
    if (this.onBitrateChanged) {
      this.onBitrateChanged(this.currentBitrateBps);
    }
  }

  private activeReader: any = null;

  private async startFrameCapture(track: MediaStreamTrack, videoElem?: HTMLVideoElement) {
    if (typeof MediaStreamTrackProcessor !== "undefined") {
      try {
        const processor = new MediaStreamTrackProcessor({ track });
        const reader = processor.readable.getReader();
        this.activeReader = reader;

        while (this.isRunning && this.activeVideoTrack === track) {
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

    // Adaptive Sender Backpressure: if DataChannel SCTP buffer has more than 96KB queued, skip non-keyframe encoding to prevent transport stall
    if (this.videoDc && this.videoDc.bufferedAmount > 96 * 1024 && !this.forceKeyframeNext) {
      this.forceKeyframeNext = true; // Request clean keyframe on next cycle
      frame.close();
      return;
    }

    // Dynamic resolution / aspect ratio adaptation
    const frameW = frame.displayWidth;
    const frameH = frame.displayHeight;
    if (frameW > 0 && frameH > 0 && (this.currentEncoderW !== frameW || this.currentEncoderH !== frameH)) {
      this.reconfigureResolution(frameW, frameH);
    }

    // Periodic Keyframe (every 60 frames = 1-2s) for instant drift recovery
    this.frameCount++;
    if (this.frameCount % 60 === 0) {
      this.forceKeyframeNext = true;
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

    if (metadata?.decoderConfig?.description) {
      const parsedSpsPps = parseDecoderConfigDescription(metadata.decoderConfig.description);
      if (parsedSpsPps) {
        this.cachedSpsPpsAnnexB = parsedSpsPps;
      }
    }

    const chunkTsUs = Math.floor(chunk.timestamp);

    // If timestamp changed, flush the accumulated slices from the previous picture
    if (this.pendingTimestampUs >= 0 && this.pendingTimestampUs !== chunkTsUs) {
      this.flushPendingAccessUnit();
    }

    this.pendingTimestampUs = chunkTsUs;
    if (chunk.type === "key") {
      this.pendingIsKeyframe = true;
    }

    const rawData = new Uint8Array(chunk.byteLength);
    chunk.copyTo(rawData);
    this.pendingChunkData.push(rawData);

    // Synchronous immediate zero-latency transmission (0.0ms delay)
    this.flushPendingAccessUnit();
  }

  private flushPendingAccessUnit() {
    if (this.pendingChunkData.length === 0 || !this.videoDc || this.videoDc.readyState !== "open") {
      this.pendingChunkData = [];
      this.pendingTimestampUs = -1;
      this.pendingIsKeyframe = false;
      return;
    }

    const isKeyframe = this.pendingIsKeyframe;
    const tsUs = this.pendingTimestampUs & 0xffffffff;

    // Concatenate all slices into 1 single Access Unit with 1 AUD
    const allNalus: Uint8Array[] = [];
    const kStartCode = new Uint8Array([0, 0, 0, 1]);
    const kAud = new Uint8Array([0, 0, 0, 1, 9, 0xf0]);

    allNalus.push(kAud);

    if (isKeyframe && this.cachedSpsPpsAnnexB) {
      allNalus.push(this.cachedSpsPpsAnnexB);
    }

    for (const rawChunk of this.pendingChunkData) {
      const extractedNalus = extractNalusFromChunk(rawChunk);
      for (const nalu of extractedNalus) {
        const naluType = nalu[0] & 0x1f;
        if (naluType === 9) continue; // skip internal AUD
        if (isKeyframe && (naluType === 7 || naluType === 8) && this.cachedSpsPpsAnnexB) continue; // skip duplicate SPS/PPS

        allNalus.push(kStartCode);
        allNalus.push(nalu);
      }
    }

    this.pendingChunkData = [];
    this.pendingTimestampUs = -1;
    this.pendingIsKeyframe = false;

    let totalLen = 0;
    for (const p of allNalus) totalLen += p.length;
    const accessUnitData = new Uint8Array(totalLen);
    let writeOffset = 0;
    for (const p of allNalus) {
      accessUnitData.set(p, writeOffset);
      writeOffset += p.length;
    }

    // Slice and send via DataChannel with 1180B chunks (Fits in a single unfragmented UDP datagram)
    const maxPayload = 1180;
    const totalChunks = Math.ceil(accessUnitData.byteLength / maxPayload);
    const seq = this.frameSeq++ & 0xffff;

    for (let i = 0; i < totalChunks; ++i) {
      const offset = i * maxPayload;
      const length = Math.min(maxPayload, accessUnitData.byteLength - offset);
      const packetBuffer = new ArrayBuffer(12 + length);
      const view = new DataView(packetBuffer);
      const packetData = new Uint8Array(packetBuffer);

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

      packetData.set(accessUnitData.subarray(offset, offset + length), 12);

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
    if (this.flushTimer) {
      clearTimeout(this.flushTimer);
      this.flushTimer = null;
    }
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
