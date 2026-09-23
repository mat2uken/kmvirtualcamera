/** Version-1 wire format: little-endian, 12-byte header, at most 255 chunks. */
export const DC_PAYLOAD_BYTES = 1180;
export const DC_MAX_CHUNKS = 255;
export const DC_MAX_AU_BYTES = DC_PAYLOAD_BYTES * DC_MAX_CHUNKS;
export type SendResult = "sent" | "too-large" | "invalid" | "send-failed";
export function sendAccessUnit(data: Uint8Array, seq: number, timestampUs: number,
  keyframe: boolean, send: (packet: ArrayBuffer) => void): SendResult {
  // Preflight the entire AU before the first send. Never wrap a chunk count in uint8.
  if (data.byteLength > DC_MAX_AU_BYTES) return "too-large";
  if (!data.byteLength || !Number.isSafeInteger(timestampUs) || !Number.isInteger(seq)) return "invalid";
  const count = Math.ceil(data.byteLength / DC_PAYLOAD_BYTES);
  for (let i = 0; i < count; ++i) {
    const part = data.subarray(i * DC_PAYLOAD_BYTES, (i + 1) * DC_PAYLOAD_BYTES);
    const buffer = new ArrayBuffer(12 + part.byteLength);
    const view = new DataView(buffer);
    view.setUint16(0, 0x4b4d, true);
    view.setUint8(2, (keyframe ? 1 : 0) | (i === 0 ? 4 : 0) | (i + 1 === count ? 8 : 0));
    view.setUint8(3, 1); view.setUint8(4, i); view.setUint8(5, count);
    view.setUint16(6, seq & 0xffff, true); view.setUint32(8, timestampUs >>> 0, true);
    new Uint8Array(buffer, 12).set(part);
    try { send(buffer); } catch { return "send-failed"; }
  }
  return "sent";
}
