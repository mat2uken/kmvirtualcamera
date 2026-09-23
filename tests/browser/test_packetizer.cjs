const assert = require('node:assert/strict');
const path = require('node:path');
const root = path.resolve(process.argv[2] || 'build/browser');
const { DC_MAX_AU_BYTES, sendAccessUnit } = require(path.join(root, 'dc_packetizer.js'));
let sent = [];
assert.equal(sendAccessUnit(new Uint8Array(DC_MAX_AU_BYTES + 1), 0, 0, true, p => sent.push(p)), 'too-large');
assert.equal(sent.length, 0);
assert.equal(sendAccessUnit(new Uint8Array(DC_MAX_AU_BYTES), 65536, 0x100000001, true, p => sent.push(p)), 'sent');
assert.equal(sent.length, 255);
for (let i = 0; i < sent.length; ++i) {
  const h = new DataView(sent[i]);
  assert.equal(h.getUint16(0, true), 0x4b4d); assert.equal(h.getUint8(4), i);
  assert.equal(h.getUint8(5), 255); assert.equal(h.getUint16(6, true), 0); assert.equal(h.getUint32(8, true), 1);
  assert.equal(sent[i].byteLength, 1192);
}
assert.equal(new DataView(sent[0]).getUint8(2), 5); assert.equal(new DataView(sent[254]).getUint8(2), 9);
sent = [];
assert.equal(sendAccessUnit(new Uint8Array(3000), 1, 0, false, p => { if (sent.length) throw Error('disconnect'); sent.push(p); }), 'send-failed');
assert.equal(sent.length, 1);
assert.equal(sendAccessUnit(new Uint8Array(), 0, 0, false, () => assert.fail()), 'invalid');
assert.equal(sendAccessUnit(new Uint8Array(1), 0, NaN, false, () => assert.fail()), 'invalid');

const encoders = [];
global.VideoEncoder = class {
  constructor(callbacks) { this.callbacks = callbacks; this.state = 'unconfigured'; this.encodeQueueSize = 0; encoders.push(this); }
  configure() { this.state = 'configured'; }
  close() { this.state = 'closed'; }
};
const { WebCodecsSender } = require(path.join(root, 'webcodecs_sender.js'));
const channels = {};
const peer = { createDataChannel(label) { return channels[label] = { readyState: 'open', bufferedAmount: 0, send(p) { if (p instanceof ArrayBuffer) sent.push(p); }, close() { this.readyState = 'closed'; } }; } };
const track = { getSettings: () => ({ width: 1280, height: 720 }), getCapabilities: () => ({}) };
const key = new Uint8Array([0,0,0,1,0x67,0x42,0x80,0,0,1,0x68,0xce,0,0,1,0x65,0xaa]);
const keyNoSets = new Uint8Array([0,0,1,0x65,0xaa]);
const delta = new Uint8Array([0,0,1,0x41,0xab]);
const description = new SharedArrayBuffer(16);
new Uint8Array(description).set([1,0x42,0,0x1e,0xff,0xe1,0,3,0x67,0x42,0x80,1,0,2,0x68,0xce]);
const chunk = (data, type='key') => ({ type, timestamp: 10000, byteLength: data.length, copyTo(dst) { dst.set(data); } });
(async () => {
  const s = new WebCodecsSender({ width:1280, height:720, fps:30, bitrateBps:3000000 }, () => {});
  s.initDataChannels(peer); await s.start(track); sent = [];
  encoders.at(-1).callbacks.output(chunk(new Uint8Array(DC_MAX_AU_BYTES + 1)), {}); assert.equal(sent.length, 0);
  encoders.at(-1).callbacks.output(chunk(delta, 'delta'), {}); assert.equal(sent.length, 0);
  encoders.at(-1).callbacks.output(chunk(keyNoSets), { decoderConfig: { description } }); assert.equal(sent.length, 1);
  const old = encoders.at(-1); await s.updateTrack({ ...track }); sent = [];
  old.callbacks.output(chunk(key), {}); assert.equal(sent.length, 0); // stale encoder output
  encoders.at(-1).callbacks.output(chunk(key), {}); assert.equal(sent.length, 1);
  const latest = encoders.at(-1); s.stop(); sent = [];
  latest.callbacks.output(chunk(key), {}); assert.equal(sent.length, 0);
  console.log('browser_protocol: 255-chunk boundary, zero-send oversize, partial-send failure, IDR gating and stale-generation tests passed');
})().catch(e => { console.error(e); process.exitCode = 1; });
