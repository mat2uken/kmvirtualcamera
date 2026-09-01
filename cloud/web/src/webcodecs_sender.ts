/**
 * WebCodecs + RTCDataChannel (Ultra-Low Latency with 50ms Realtime Deadline)
 * Features:
 * - Deterministic AVCC vs Annex-B parser using bitstream structure verification
 * - Single-Initialization DataChannels with 50ms MaxPacketLifeTime (Micro-Loss Recovery)
 * - Dynamic Native Aspect-Ratio & Orientation Detection
 * - Strict AUD (Access Unit Delimiter) injection on every frame for MFT boundary lock
 * - Periodic Intra-refresh (Keyframe every 30 frames) & Instant PLI recovery
 * - Adaptive Bitrate (ABR) & Constant Bitrate (CBR) mode to prevent motion packet spikes
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

function hasSpsNalu(data: Uint8Array): boolean {
  for (let i = 0; i < data.length - 4; i++) {
    if (data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 0 && data[i + 3] === 1) {
      const naluType = data[i + 4] & 0x1f;
      if (naluType === 7) return true;
    }
  }
  return false;
}

function hasAud(data: Uint8Array): boolean {
  if (data.length < 5) return false;
  if (data[0] === 0 && data[1] === 0 && data[2] === 0 && data[3] === 1) {
    return (data[4] & 0x1f) === 9;
  }
  if (data[0] === 0 && data[1] === 0 && data[2] === 1) {
    return (data[3] & 0x1f) === 9;
  }
  return false;
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

function normalizeChunkToAnnexB(chunkData: Uint8Array, spsPpsAnnexB: Uint8Array | null, isKeyframe: boolean): Uint8Array {
  const kAud = new Uint8Array([0, 0, 0, 1, 9, 0xf0]);

  // Check if chunk is AVCC (4-byte length prefix)
  if (isAvccFormat(chunkData)) {
    const naluList: Uint8Array[] = [];
    let totalLen = 0;

    if (isKeyframe && spsPpsAnnexB) {
      naluList.push(spsPpsAnnexB);
      totalLen += spsPpsAnnexB.length;
    }

    let offset = 0;
    const view = new DataView(chunkData.buffer, chunkData.byteOffset, chunkData.byteLength);
    while (offset + 4 <= chunkData.byteLength) {
      const naluLen = view.getUint32(offset);
      offset += 4;
      if (offset + naluLen > chunkData.byteLength) break;
      naluList.push(new Uint8Array([0, 0, 0, 1]));
      naluList.push(chunkData.subarray(offset, offset + naluLen));
      totalLen += 4 + naluLen;
      offset += naluLen;
    }

    const out = new Uint8Array(kAud.length + totalLen);
    out.set(kAud, 0);
    let writeOffset = kAud.length;
    for (const item of naluList) {
      out.set(item, writeOffset);
      writeOffset += item.length;
    }
    return out;
  }

  // Already Annex-B format
  let payload = chunkData;
  if (isKeyframe && spsPpsAnnexB && !hasSpsNalu(chunkData)) {
    const out = new Uint8Array(spsPpsAnnexB.length + chunkData.length);
    out.set(spsPpsAnnexB, 0);
    out.set(chunkData, spsPpsAnnexB.length);
    payload = out;
  }

  if (!hasAud(payload)) {
    const finalOut = new Uint8Array(kAud.length + payload.length);
    finalOut.set(kAud, 0);
    finalOut.set(payload, kAud.length);
    return finalOut;
  }
  return payload;
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

    // 1. Create In-Order Video DataChannel with 50ms Realtime Deadline (Micro-Retransmit for 100% clean frames)
    this.videoDc = pc.createDataChannel("km-video-stream", {
      ordered: true,
      maxPacketLifeTime: 50
    });
    this.videoDc.binaryType = "arraybuffer";

    // 2. Create Reliable Control DataChannel (for PLI / ABR / Stats)
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
    this.frameSeq = 0;
    this.frameCount = 0;
    this.forceKeyframeNext = true;
    this.cachedSpsPpsAnnexB = null;

    // Detect initial track dimensions (Portrait / Landscape awareness)
    const settings = track.getSettings();
    const initialW = settings.width || this.config.width;
    const initialH = settings.height || this.config.height;

    this.initEncoder(initialW, initialH);

    // Ingest video frames from TrackProcessor or Video element fallback
    this.startFrameCapture(track, videoElem);
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

  private setupControlChannel(dc: RTCDataChannel) {
    dc.onopen = () => {
      this.logFn("[WebCodecs] Control DataChannel open.");
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
          this.forceKeyframeNext = true;
          this.logFn("[WebCodecs] Instant PLI Keyframe requested by receiver.");
        } else if (msg.type === "bitrate" && typeof msg.bps === "number") {
          this.setBitrate(msg.bps);
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
    this.currentBitrateBps = Math.max(500000, Math.min(8000000, newBps));
    if (this.currentEncoderW > 0 && this.currentEncoderH > 0) {
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
    }
    if (this.onBitrateChanged) {
      this.onBitrateChanged(this.currentBitrateBps);
    }
  }

  private async startFrameCapture(track: MediaStreamTrack, videoElem?: HTMLVideoElement) {
    if (typeof MediaStreamTrackProcessor !== "undefined") {
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

    // Dynamic resolution / aspect ratio adaptation
    const frameW = frame.displayWidth;
    const frameH = frame.displayHeight;
    if (frameW > 0 && frameH > 0 && (this.currentEncoderW !== frameW || this.currentEncoderH !== frameH)) {
      this.reconfigureResolution(frameW, frameH);
    }

    // Periodic Keyframe (every 30 frames = 1s) for fast recovery
    this.frameCount++;
    if (this.frameCount % 30 === 0) {
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

    // Extract SPS/PPS from metadata description if available
    if (metadata?.decoderConfig?.description) {
      const parsedSpsPps = parseDecoderConfigDescription(metadata.decoderConfig.description);
      if (parsedSpsPps) {
        this.cachedSpsPpsAnnexB = parsedSpsPps;
      }
    }

    const isKeyframe = chunk.type === "key";
    const rawChunkData = new Uint8Array(chunk.byteLength);
    chunk.copyTo(rawChunkData);

    // Normalize bitstream to standard Annex-B format (00 00 00 01)
    const annexBData = normalizeChunkToAnnexB(rawChunkData, this.cachedSpsPpsAnnexB, isKeyframe);

    // Slice and send via DataChannel with 2KB chunks
    const maxPayload = 2048;
    const totalChunks = Math.ceil(annexBData.byteLength / maxPayload);
    const seq = this.frameSeq++ & 0xffff;
    const tsUs = Math.floor(chunk.timestamp) & 0xffffffff;

    for (let i = 0; i < totalChunks; ++i) {
      const offset = i * maxPayload;
      const length = Math.min(maxPayload, annexBData.byteLength - offset);
      const packetBuffer = new ArrayBuffer(12 + length);
      const view = new DataView(packetBuffer);
      const packetData = new Uint8Array(packetBuffer);

      // Header: 12 Bytes (Little Endian)
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

      packetData.set(annexBData.subarray(offset, offset + length), 12);

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
