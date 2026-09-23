import { DC_MAX_AU_BYTES, sendAccessUnit } from "./dc_packetizer";

export interface WebCodecsSenderConfig { width: number; height: number; fps: number; bitrateBps: number; }
// This is an H.264 access-unit sender, not a promise of zero latency or one UDP datagram.
function nalus(data: Uint8Array): Uint8Array[] {
  if (!data.length || data.length > DC_MAX_AU_BYTES) return [];
  const start = (p: number) => p + 3 <= data.length && data[p] === 0 && data[p + 1] === 0
    ? data[p + 2] === 1 ? 3 : p + 4 <= data.length && data[p + 2] === 0 && data[p + 3] === 1 ? 4 : 0 : 0;
  const valid = (n: Uint8Array) => n.length > 0 && !(n[0] & 128) && (n[0] & 31) > 0 && (n[0] & 31) < 24;
  const out: Uint8Array[] = [];
  if (start(0)) {
    let pos = 0;
    while (pos < data.length) {
      const size = start(pos); if (!size) return [];
      const begin = pos + size; let next = begin;
      while (next < data.length && !start(next)) ++next;
      let end = next; while (end > begin && data[end - 1] === 0) --end;
      const n = data.subarray(begin, end); if (!valid(n) || out.length >= 1024) return [];
      out.push(n); pos = next;
    }
  } else {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength); let pos = 0;
    while (pos < data.length) {
      if (data.length - pos < 4) return [];
      const size = view.getUint32(pos); pos += 4;
      if (!size || size > data.length - pos || out.length >= 1024) return [];
      const n = data.subarray(pos, pos + size); if (!valid(n)) return [];
      out.push(n); pos += size;
    }
  }
  return out;
}
function parameterSets(description: AllowSharedBufferSource): Uint8Array[] {
  const data = ArrayBuffer.isView(description)
    ? new Uint8Array(description.buffer, description.byteOffset, description.byteLength)
    : new Uint8Array(description);
  if (data.length > 131072) return [];
  if (data.length >= 3 && data[0] === 0 && data[1] === 0) return nalus(data).filter(n => [7, 8].includes(n[0] & 31));
  if (data.length < 7 || data[0] !== 1 || (data[4] & 3) !== 3) return [];
  let pos = 6; const out: Uint8Array[] = [];
  for (let kind = 0; kind < 2; ++kind) {
    if (kind && pos >= data.length) return [];
    const count = kind ? data[pos++] : data[5] & 31;
    for (let i = 0; i < count; ++i) {
      if (data.length - pos < 2) return [];
      const size = (data[pos] << 8) | data[pos + 1]; pos += 2;
      if (!size || size > data.length - pos || (data[pos] & 31) !== (kind ? 8 : 7) || (data[pos] & 128)) return [];
      out.push(data.slice(pos, pos + size)); pos += size;
    }
  }
  return out;
}
export class WebCodecsSender {
  private encoder: VideoEncoder | null = null;
  private videoDc: RTCDataChannel | null = null;
  private controlDc: RTCDataChannel | null = null;
  private isRunning = false;
  private frameSeq = 0;
  private frameCount = 0;
  private forceKeyframeNext = true;
  private waitingForKeyframe = true;
  private currentBitrateBps: number;
  private currentEncoderW = 0;
  private currentEncoderH = 0;
  private rttMs = 0;
  private lastPingTime = 0;
  private cachedSpsPps: Uint8Array[] = [];
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private activeReader: ReadableStreamDefaultReader<VideoFrame> | null = null;
  private captureGeneration = 0;
  private encoderGeneration = 0;
  private oversizeCount = 0;
  public activeVideoTrack: MediaStreamTrack | null = null;
  public onRemoteCommand?: (cmd: string, payload: any) => void;
  private onBitrateChanged?: (bps: number) => void;
  private onStatsUpdate?: (stats: { fps: number; bitrateBps: number; rttMs: number; bufferedKB: number }) => void;
  constructor(private config: WebCodecsSenderConfig, private logFn: (message: string) => void = console.log) {
    this.currentBitrateBps = config.bitrateBps;
  }
  static isSupported(): boolean { return typeof window !== "undefined" && typeof VideoEncoder !== "undefined" && typeof EncodedVideoChunk !== "undefined"; }
  setCallbacks(onBitrateChanged?: (bps: number) => void, onStatsUpdate?: (stats: { fps: number; bitrateBps: number; rttMs: number; bufferedKB: number }) => void) {
    this.onBitrateChanged = onBitrateChanged; this.onStatsUpdate = onStatsUpdate;
  }
  initDataChannels(pc: RTCPeerConnection) {
    if (this.videoDc || this.controlDc) return;
    this.videoDc = pc.createDataChannel("km-video-stream", { ordered: false, maxPacketLifeTime: 50 });
    this.videoDc.binaryType = "arraybuffer";
    this.controlDc = pc.createDataChannel("km-control", { ordered: true });
    const dc = this.controlDc;
    dc.onopen = () => {
      if (this.controlDc !== dc) return;
      this.sendCameraCapabilities();
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = setInterval(() => {
        if (this.controlDc === dc && dc.readyState === "open") {
          this.lastPingTime = performance.now();
          try { dc.send(JSON.stringify({ type: "ping", ts: Date.now() })); } catch {}
        }
      }, 2000);
    };
    dc.onclose = () => { if (this.controlDc === dc && this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; } };
    dc.onmessage = async (event) => {
      if (this.controlDc !== dc || typeof event.data !== "string" || event.data.length > 65536) return;
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === "pli") this.forceKeyframeNext = true;
        else if (msg.type === "bitrate" && Number.isFinite(msg.bps)) this.setBitrate(msg.bps);
        else if (msg.type === "remote_control") await this.handleRemoteControl(msg);
        else if (msg.type === "query_caps") this.sendCameraCapabilities();
        else if (msg.type === "pong" && this.lastPingTime) this.rttMs = Math.round(performance.now() - this.lastPingTime);
      } catch {}
    };
  }
  async start(track: MediaStreamTrack, videoElem?: HTMLVideoElement): Promise<void> {
    this.isRunning = true; this.frameSeq = this.frameCount = this.oversizeCount = 0;
    await this.updateTrack(track, videoElem);
  }
  async updateTrack(track: MediaStreamTrack, videoElem?: HTMLVideoElement): Promise<void> {
    const capture = ++this.captureGeneration; this.activeVideoTrack = track;
    const old = this.activeReader; this.activeReader = null;
    if (old) { try { await old.cancel(); } catch {} }
    if (!this.isRunning || capture !== this.captureGeneration) return;
    const settings = track.getSettings();
    this.initEncoder(settings.width || this.config.width, settings.height || this.config.height);
    this.sendCameraCapabilities();
    void this.capture(track, videoElem, capture);
  }
  private initEncoder(width: number, height: number) {
    ++this.encoderGeneration;
    if (this.encoder) { try { this.encoder.close(); } catch {} }
    this.cachedSpsPps = []; this.waitingForKeyframe = this.forceKeyframeNext = true;
    this.currentEncoderW = width; this.currentEncoderH = height;
    const generation = this.encoderGeneration;
    this.encoder = new VideoEncoder({
      output: (chunk, metadata) => { if (this.isRunning && generation === this.encoderGeneration) this.handleEncodedChunk(chunk, metadata); },
      error: (error) => { if (generation === this.encoderGeneration) { this.waitingForKeyframe = this.forceKeyframeNext = true; this.logFn(`[WebCodecs] ${error.message}`); } }
    });
    this.configureEncoder();
  }
  private configureEncoder() {
    this.encoder?.configure({ codec: "avc1.420028", width: this.currentEncoderW, height: this.currentEncoderH,
      bitrate: this.currentBitrateBps, framerate: this.config.fps, bitrateMode: "constant", latencyMode: "realtime",
      hardwareAcceleration: "prefer-hardware", avc: { format: "annexb" } });
  }
  setBitrate(bps: number) {
    if (!Number.isFinite(bps)) return;
    const next = Math.max(1500000, Math.min(8000000, bps));
    if (next === this.currentBitrateBps) return;
    this.currentBitrateBps = next; this.forceKeyframeNext = true;
    try { if (this.encoder?.state === "configured") this.configureEncoder(); } catch (error) { this.logFn(`[WebCodecs] configure failed: ${error}`); }
    this.onBitrateChanged?.(next);
  }
  private async capture(track: MediaStreamTrack, element: HTMLVideoElement | undefined, generation: number) {
    const active = () => this.isRunning && generation === this.captureGeneration && this.activeVideoTrack === track;
    const Processor = (globalThis as any).MediaStreamTrackProcessor;
    if (Processor) {
      let reader: ReadableStreamDefaultReader<VideoFrame> | null = null;
      try {
        reader = new Processor({ track }).readable.getReader(); this.activeReader = reader;
        while (active()) {
          const { done, value } = await reader!.read();
          if (value) { if (active()) this.processVideoFrame(value); else value.close(); }
          if (done) break;
        }
        return;
      } catch (error) { if (active()) this.logFn(`[WebCodecs] TrackProcessor fallback: ${error}`); }
      finally { if (this.activeReader === reader) this.activeReader = null; try { reader?.releaseLock(); } catch {} }
    }
    if (active() && element && "requestVideoFrameCallback" in element) {
      const tick = () => {
        if (!active()) return;
        try { this.processVideoFrame(new VideoFrame(element, { timestamp: Math.round(performance.now() * 1000) })); } catch {}
        if (active()) element.requestVideoFrameCallback(tick);
      };
      element.requestVideoFrameCallback(tick);
    }
  }
  private processVideoFrame(frame: VideoFrame) {
    try {
      if (!this.isRunning || this.encoder?.state !== "configured") return;
      if (!this.videoDc || this.videoDc.readyState !== "open" || this.videoDc.bufferedAmount > 96 * 1024 || this.encoder.encodeQueueSize > 2) {
        this.forceKeyframeNext = true; return;
      }
      if (frame.displayWidth !== this.currentEncoderW || frame.displayHeight !== this.currentEncoderH) this.initEncoder(frame.displayWidth, frame.displayHeight);
      if (++this.frameCount % 60 === 0) this.forceKeyframeNext = true;
      const keyFrame = this.forceKeyframeNext; this.forceKeyframeNext = false;
      this.encoder!.encode(frame, { keyFrame });
    } catch (error) { this.forceKeyframeNext = true; this.logFn(`[WebCodecs] encode failed: ${error}`); }
    finally { frame.close(); }
  }
  private recoverOversize() {
    this.waitingForKeyframe = this.forceKeyframeNext = true;
    this.setBitrate(this.currentBitrateBps * 0.75);
    if (++this.oversizeCount >= 3) {
      this.logFn("[WebCodecs] Access unit exceeds the v1 300900-byte limit repeatedly; stop and select a lower resolution.");
      this.stop();
    } else this.logFn("[WebCodecs] Oversized access unit dropped before transmission; bitrate reduced and IDR requested.");
  }
  private handleEncodedChunk(chunk: EncodedVideoChunk, metadata?: EncodedVideoChunkMetadata) {
    const dc = this.videoDc;
    if (!dc || dc.readyState !== "open") { this.waitingForKeyframe = this.forceKeyframeNext = true; return; }
    if (chunk.byteLength > DC_MAX_AU_BYTES) { this.recoverOversize(); return; }
    if (metadata?.decoderConfig?.description) {
      const sets = parameterSets(metadata.decoderConfig.description);
      if (sets.some(n => (n[0] & 31) === 7) && sets.some(n => (n[0] & 31) === 8)) this.cachedSpsPps = sets;
    }
    const bytes = new Uint8Array(chunk.byteLength); chunk.copyTo(bytes);
    const units = nalus(bytes); const idr = units.some(n => (n[0] & 31) === 5);
    if (!units.length || (chunk.type === "key" && !idr)) { this.waitingForKeyframe = this.forceKeyframeNext = true; return; }
    if (this.waitingForKeyframe && !idr) return;
    const inBand = units.filter(n => [7, 8].includes(n[0] & 31));
    if (inBand.length) {
      if (!inBand.some(n => (n[0] & 31) === 7) || !inBand.some(n => (n[0] & 31) === 8)) { this.cachedSpsPps = []; this.waitingForKeyframe = this.forceKeyframeNext = true; return; }
      this.cachedSpsPps = inBand.map(n => n.slice());
    }
    if (idr && !this.cachedSpsPps.length) { this.waitingForKeyframe = this.forceKeyframeNext = true; return; }
    const selected = [...(idr ? this.cachedSpsPps : []), ...units.filter(n => (n[0] & 31) !== 9 && (!idr || ![7, 8].includes(n[0] & 31)))];
    const total = 6 + selected.reduce((sum, n) => sum + 4 + n.length, 0);
    if (total > DC_MAX_AU_BYTES) { this.recoverOversize(); return; }
    const au = new Uint8Array(total); au.set([0, 0, 0, 1, 9, 0xf0]); let offset = 6;
    for (const n of selected) { au.set([0, 0, 0, 1], offset); au.set(n, offset + 4); offset += 4 + n.length; }
    const result = sendAccessUnit(au, this.frameSeq++ & 0xffff, Math.floor(chunk.timestamp), idr, p => dc.send(p));
    if (result !== "sent") { this.waitingForKeyframe = this.forceKeyframeNext = true; return; }
    if (idr) this.waitingForKeyframe = false;
    this.oversizeCount = 0;
    this.onStatsUpdate?.({ fps: this.config.fps, bitrateBps: this.currentBitrateBps, rttMs: this.rttMs, bufferedKB: Math.round(dc.bufferedAmount / 1024) });
  }
  sendCameraCapabilities() {
    if (!this.controlDc || this.controlDc.readyState !== "open" || !this.activeVideoTrack) return;
    try {
      const caps = (this.activeVideoTrack.getCapabilities?.() || {}) as any;
      const settings = this.activeVideoTrack.getSettings() as any;
      this.controlDc.send(JSON.stringify({ type: "camera_caps", supportsTorch: !!caps.torch,
        minZoom: caps.zoom?.min || 1, maxZoom: caps.zoom?.max || 1, currentZoom: settings.zoom || 1, facingMode: settings.facingMode || "environment" }));
    } catch {}
  }
  private async handleRemoteControl(msg: any) {
    const track = this.activeVideoTrack; if (!track) return;
    try {
      if (msg.cmd === "torch") await track.applyConstraints({ advanced: [{ torch: !!msg.enabled } as any] });
      else if (msg.cmd === "zoom" && Number.isFinite(msg.value)) await track.applyConstraints({ advanced: [{ zoom: msg.value } as any] });
      else if (msg.cmd === "switch_camera") await this.onRemoteCommand?.("switch_camera", msg);
    } catch (error) { this.logFn(`[WebCodecs] Remote control failed: ${error}`); }
  }
  stop() {
    this.isRunning = false; ++this.captureGeneration; ++this.encoderGeneration;
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    const reader = this.activeReader; this.activeReader = null;
    if (reader) void reader.cancel().catch(() => {});
    this.activeVideoTrack = null;
    for (const dc of [this.videoDc, this.controlDc]) { if (dc) { dc.onopen = dc.onclose = dc.onmessage = null; try { dc.close(); } catch {} } }
    this.videoDc = this.controlDc = null;
    if (this.encoder) { try { this.encoder.close(); } catch {} this.encoder = null; }
    this.cachedSpsPps = [];
  }
}
