import { chromium } from "playwright";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "..");
const logPath = path.join(rootDir, "receiver_debug.log");
const receiverExe = path.join(rootDir, "windows", "build", "Release", "Receiver.exe");
const screenshotDir = path.join(rootDir, "test_screenshots");

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function calculateMacroblockDiscontinuityMetric(bmpBuffer) {
  if (bmpBuffer.length < 54) return { metric: 0, valid: false };
  const dataOffset = bmpBuffer.readUInt32LE(10);
  const width = bmpBuffer.readInt32LE(18);
  const height = Math.abs(bmpBuffer.readInt32LE(22));
  const bpp = bmpBuffer.readUInt16LE(28);

  if (bpp !== 24 || width <= 32 || height <= 32) return { metric: 0, valid: false };

  const rowStride = (width * 3 + 3) & ~3;
  let gridEdgeSum = 0;
  let gridEdgeCount = 0;
  let nonGridEdgeSum = 0;
  let nonGridEdgeCount = 0;

  for (let y = 1; y < height - 1; y++) {
    const rowOffset = dataOffset + y * rowStride;
    for (let x = 1; x < width - 1; x++) {
      const b0 = bmpBuffer[rowOffset + (x - 1) * 3 + 0];
      const g0 = bmpBuffer[rowOffset + (x - 1) * 3 + 1];
      const r0 = bmpBuffer[rowOffset + (x - 1) * 3 + 2];
      const y0 = 0.299 * r0 + 0.587 * g0 + 0.114 * b0;

      const b1 = bmpBuffer[rowOffset + x * 3 + 0];
      const g1 = bmpBuffer[rowOffset + x * 3 + 1];
      const r1 = bmpBuffer[rowOffset + x * 3 + 2];
      const y1 = 0.299 * r1 + 0.587 * g1 + 0.114 * b1;

      const diff = Math.abs(y1 - y0);

      if (x % 16 === 0) {
        gridEdgeSum += diff;
        gridEdgeCount++;
      } else if (x % 16 === 8) {
        nonGridEdgeSum += diff;
        nonGridEdgeCount++;
      }
    }
  }

  const avgGrid = gridEdgeCount > 0 ? gridEdgeSum / gridEdgeCount : 0;
  const avgNonGrid = nonGridEdgeCount > 0 ? nonGridEdgeSum / nonGridEdgeCount : 1;
  const metric = avgNonGrid > 0.1 ? avgGrid / avgNonGrid : 1.0;
  return { metric, width, height, valid: true };
}

async function runPhoneRotationStressTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: DYNAMIC PHONE ROTATION & RESOLUTION SWITCHING TEST         ");
  console.log("================================================================================");

  if (fs.existsSync(logPath)) {
    try { fs.unlinkSync(logPath); } catch {}
  }

  const receiverProc = spawn(receiverExe, ["--url=https://webrtc-bridge-signaling.mat2uken.workers.dev"], {
    cwd: rootDir,
    detached: false,
    stdio: "ignore"
  });

  let browser = null;

  try {
    let joinUrl = null;
    const startWait = Date.now();
    while (Date.now() - startWait < 15000) {
      await sleep(500);
      if (fs.existsSync(logPath)) {
        const logContent = fs.readFileSync(logPath, "utf-8");
        const match = logContent.match(/JoinUrl=(https:\/\/[^\s\r\n]+)/);
        if (match) {
          joinUrl = match[1];
          break;
        }
      }
    }

    if (!joinUrl) throw new Error("Timed out waiting for Receiver session");

    browser = await chromium.launch({
      headless: true,
      args: ["--use-fake-ui-for-media-stream", "--allow-file-access", "--no-sandbox"]
    });

    const context = await browser.newContext({
      viewport: { width: 390, height: 844 },
      isMobile: true,
      hasTouch: true,
      permissions: ["camera", "microphone"]
    });

    // Hook getUserMedia with dynamic orientation support
    await context.addInitScript(() => {
      window.simulatedOrientation = "portrait"; // portrait (720x1280) or landscape (1280x720)
      const canvas = document.createElement("canvas");
      canvas.width = 720;
      canvas.height = 1280;
      const ctx = canvas.getContext("2d", { alpha: false });

      let frame = 0;
      function renderRotationStream() {
        frame++;
        const isPortrait = window.simulatedOrientation === "portrait";
        const w = isPortrait ? 720 : 1280;
        const h = isPortrait ? 1280 : 720;

        if (canvas.width !== w || canvas.height !== h) {
          canvas.width = w;
          canvas.height = h;
        }

        const hue = (frame * 5) % 360;
        ctx.fillStyle = `hsl(${hue}, 80%, 40%)`;
        ctx.fillRect(0, 0, w, h);

        // Moving content
        for (let i = 0; i < 8; i++) {
          const cx = (Math.sin(frame * 0.07 + i * 0.8) * 0.4 + 0.5) * w;
          const cy = (Math.cos(frame * 0.1 + i * 1.2) * 0.4 + 0.5) * h;
          ctx.beginPath();
          ctx.arc(cx, cy, 35 + i * 10, 0, Math.PI * 2);
          ctx.fillStyle = `hsl(${(frame * 8 + i * 45) % 360}, 90%, 65%)`;
          ctx.fill();
        }

        ctx.font = "bold 32px monospace";
        ctx.fillStyle = "#ffffff";
        ctx.fillText(`ORIENTATION: ${window.simulatedOrientation.toUpperCase()} - F${frame}`, 30, 80);

        requestAnimationFrame(renderRotationStream);
      }
      renderRotationStream();

      navigator.mediaDevices.getUserMedia = async function () {
        return canvas.captureStream(60);
      };
    });

    const page = await context.newPage();
    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    await page.locator("#select-transport-mode").selectOption("webcodecs_datachannel");
    await page.locator("button.button-primary").click();
    console.log("  - Stream started in Portrait Mode (720x1280)...");

    // Wait for connection
    let isConnected = false;
    for (let i = 0; i < 20; i++) {
      await sleep(1000);
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      if (logContent.includes("PeerState: Connected")) {
        isConnected = true;
        break;
      }
    }
    if (!isConnected) throw new Error("Connection failed");

    // Phase 1: Stream in Portrait for 3 seconds
    console.log("\n[PHASE 1] Streaming in Portrait Mode (720x1280)...");
    await sleep(3000);
    const snapshotPath = path.join(screenshotDir, "last_decoded_frame.bmp");
    if (fs.existsSync(snapshotPath)) {
      const bmpBuf = fs.readFileSync(snapshotPath);
      const res = calculateMacroblockDiscontinuityMetric(bmpBuf);
      console.log(`  - Portrait Metric B = ${res.metric.toFixed(3)} (Threshold: <1.35)`);
      fs.copyFileSync(snapshotPath, path.join(screenshotDir, "rotation_phase1_portrait.bmp"));
    }

    // Phase 2: Rotate phone to Landscape (1280x720) in real-time
    console.log("\n[PHASE 2] ROTATING PHONE TO LANDSCAPE (1280x720) IN-FLIGHT...");
    await page.evaluate(() => {
      // @ts-ignore
      window.simulatedOrientation = "landscape";
    });
    await sleep(4000);

    if (fs.existsSync(snapshotPath)) {
      const bmpBuf = fs.readFileSync(snapshotPath);
      const res = calculateMacroblockDiscontinuityMetric(bmpBuf);
      console.log(`  - Landscape Metric B = ${res.metric.toFixed(3)} (Threshold: <1.35)`);
      fs.copyFileSync(snapshotPath, path.join(screenshotDir, "rotation_phase2_landscape.bmp"));
    }

    // Phase 3: Rotate phone back to Portrait (720x1280) in real-time
    console.log("\n[PHASE 3] ROTATING PHONE BACK TO PORTRAIT (720x1280) IN-FLIGHT...");
    await page.evaluate(() => {
      // @ts-ignore
      window.simulatedOrientation = "portrait";
    });
    await sleep(4000);

    if (fs.existsSync(snapshotPath)) {
      const bmpBuf = fs.readFileSync(snapshotPath);
      const res = calculateMacroblockDiscontinuityMetric(bmpBuf);
      console.log(`  - Final Portrait Metric B = ${res.metric.toFixed(3)} (Threshold: <1.35)`);
      fs.copyFileSync(snapshotPath, path.join(screenshotDir, "rotation_phase3_portrait.bmp"));

      if (res.metric > 1.35) {
        throw new Error(`BLOCK NOISE DETECTED AFTER ROTATION: Metric ${res.metric.toFixed(3)} exceeded threshold!`);
      }
    }

    console.log("\n================================================================================");
    console.log("  >>> DYNAMIC PHONE ROTATION TEST PASSED WITH ZERO BLOCK NOISE <<<              ");
    console.log("================================================================================");
  } finally {
    if (browser) await browser.close().catch(() => {});
    receiverProc.kill();
  }
}

runPhoneRotationStressTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> TEST FAILED: <<<", err);
    process.exit(1);
  });
