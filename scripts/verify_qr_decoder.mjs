import fs from "fs";
import path from "path";
import { fileURLToPath, pathToFileURL } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const jsqrPath = path.resolve(__dirname, "../cloud/node_modules/jsqr/dist/jsQR.js");

let jsQR;
if (fs.existsSync(jsqrPath)) {
  const mod = await import(pathToFileURL(jsqrPath).href);
  jsQR = mod.default || mod;
} else {
  const mod = await import("jsqr");
  jsQR = mod.default || mod;
}

const imgPath = process.argv[2];
if (!imgPath || !fs.existsSync(imgPath)) {
  console.error("Usage: node verify_qr_decoder.mjs <image.bmp>");
  process.exit(1);
}

// Parse BMP 24-bit file
const buf = fs.readFileSync(imgPath);
if (buf.readUInt16LE(0) !== 0x4D42) {
  console.error("Invalid BMP file header");
  process.exit(1);
}

const offset = buf.readUInt32LE(10);
const width = buf.readInt32LE(18);
const height = Math.abs(buf.readInt32LE(22));
const rowSize = Math.floor((width * 3 + 3) / 4) * 4;

const rgba = new Uint8ClampedArray(width * height * 4);
for (let y = 0; y < height; y++) {
  // BMP rows are bottom-to-top
  const srcRow = height - 1 - y;
  const srcOffset = offset + srcRow * rowSize;
  for (let x = 0; x < width; x++) {
    const b = buf[srcOffset + x * 3 + 0];
    const g = buf[srcOffset + x * 3 + 1];
    const r = buf[srcOffset + x * 3 + 2];
    const dstIdx = (y * width + x) * 4;
    rgba[dstIdx + 0] = r;
    rgba[dstIdx + 1] = g;
    rgba[dstIdx + 2] = b;
    rgba[dstIdx + 3] = 255;
  }
}

const code = jsQR(rgba, width, height);
if (code && code.data) {
  console.log(`[AUTOMATED TEST PASS] Successfully decoded QR code with jsQR!`);
  console.log(`Decoded Content: ${code.data}`);
  process.exit(0);
} else {
  console.error(`[FAIL] jsQR could not decode QR code from ${imgPath}`);
  process.exit(1);
}
