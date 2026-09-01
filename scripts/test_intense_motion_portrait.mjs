import { chromium } from "playwright";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "..");
const logPath = path.join(rootDir, "receiver_debug.log");
const receiverExe = path.join(rootDir, "windows", "build", "Release", "Receiver.exe");

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function getDecodedFrameCount() {
  if (!fs.existsSync(logPath)) return 0;
  try {
    const logContent = fs.readFileSync(logPath, "utf-8");
    const matches = [...logContent.matchAll(/Stream active: (\d+) frames decoded/g)];
    if (matches.length > 0) {
      return parseInt(matches[matches.length - 1][1], 10);
    }
  } catch {}
  return 0;
}

async function runIntenseMotionPortraitTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: PORTRAIT (9:16) INTENSE MOTION STRESS AUTOMATED TEST       ");
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

    if (!joinUrl) throw new Error("Timed out waiting for Receiver session initialization");

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

    // Inject portrait high-motion stream (720x1280 @ 60fps)
    await context.addInitScript(() => {
      navigator.mediaDevices.getUserMedia = async function () {
        const width = 720;
        const height = 1280;
        const fps = 60;

        const canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext("2d", { alpha: false });

        let frame = 0;
        function renderPortraitMotion() {
          frame++;
          const hue = (frame * 6) % 360;
          const grad = ctx.createLinearGradient(0, 0, width, height);
          grad.addColorStop(0, `hsl(${hue}, 90%, 45%)`);
          grad.addColorStop(1, `hsl(${(hue + 180) % 360}, 90%, 35%)`);
          ctx.fillStyle = grad;
          ctx.fillRect(0, 0, width, height);

          // Moving circles in vertical portrait orientation
          for (let i = 0; i < 10; i++) {
            const cx = (Math.sin(frame * 0.08 + i * 0.9) * 0.45 + 0.5) * width;
            const cy = (Math.cos(frame * 0.12 + i * 1.1) * 0.45 + 0.5) * height;
            const r = 25 + (i % 3) * 15;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.fillStyle = `hsl(${(frame * 9 + i * 36) % 360}, 100%, 70%)`;
            ctx.fill();
            ctx.lineWidth = 4;
            ctx.strokeStyle = "#ffffff";
            ctx.stroke();
          }

          ctx.font = "bold 32px monospace";
          ctx.fillStyle = "#ffffff";
          ctx.fillText(`PORTRAIT STRESS - ${frame}`, 30, 80);

          requestAnimationFrame(renderPortraitMotion);
        }
        renderPortraitMotion();

        return canvas.captureStream(fps);
      };
    });

    const page = await context.newPage();
    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    await page.locator("#select-transport-mode").selectOption("webcodecs_datachannel");
    await page.locator("button.button-primary").click();
    console.log("  - Clicked '送信開始' in Portrait High-Motion test...");

    let isConnected = false;
    for (let i = 0; i < 20; i++) {
      await sleep(1000);
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      if (logContent.includes("PeerState: Connected")) {
        isConnected = true;
        console.log("  [PASS] WebRTC Connected in Portrait mode!");
        break;
      }
    }
    if (!isConnected) throw new Error("Connection failed");

    console.log("  Streaming Portrait High-Motion frames for 10 seconds...");
    const testStart = Date.now();
    let prevFrames = 0;
    while (Date.now() - testStart < 10000) {
      await sleep(1500);
      const curFrames = getDecodedFrameCount();
      const delta = curFrames - prevFrames;
      prevFrames = curFrames;
      const elapsed = ((Date.now() - testStart) / 1000).toFixed(1);
      console.log(`    [T+${elapsed}s] Decoded Portrait Frames: ${curFrames} (+${delta} frames/1.5s)`);
    }

    const finalFrames = getDecodedFrameCount();
    if (finalFrames < 300) {
      throw new Error(`Insufficient portrait frames decoded: ${finalFrames}`);
    }

    console.log("\n================================================================================");
    console.log(`  >>> PORTRAIT INTENSE MOTION TEST PASSED: ${finalFrames} FRAMES DECODED <<<     `);
    console.log("================================================================================");
  } finally {
    if (browser) await browser.close().catch(() => {});
    receiverProc.kill();
  }
}

runIntenseMotionPortraitTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> PORTRAIT STRESS TEST FAILED: <<<", err);
    process.exit(1);
  });
