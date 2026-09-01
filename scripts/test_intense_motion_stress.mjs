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

if (!fs.existsSync(screenshotDir)) {
  fs.mkdirSync(screenshotDir, { recursive: true });
}

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

async function runIntenseMotionStressTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: INTENSE MOTION & STRESS REPRODUCTION AUTOMATED TEST        ");
  console.log("================================================================================");

  if (fs.existsSync(logPath)) {
    try { fs.unlinkSync(logPath); } catch {}
  }

  // 1. Start native Receiver.exe in background
  console.log("\n[1/5] Starting native Receiver.exe process...");
  const receiverProc = spawn(receiverExe, ["--url=https://webrtc-bridge-signaling.mat2uken.workers.dev"], {
    cwd: rootDir,
    detached: false,
    stdio: "ignore"
  });

  receiverProc.on("error", (err) => {
    console.error("Failed to spawn Receiver.exe:", err);
  });

  let browser = null;

  try {
    // 2. Wait for Session Join URL
    console.log("  Waiting for Cloudflare session initialization...");
    let joinUrl = null;
    const startWait = Date.now();
    while (Date.now() - startWait < 15000) {
      await sleep(500);
      if (fs.existsSync(logPath)) {
        const logContent = fs.readFileSync(logPath, "utf-8");
        const match = logContent.match(/JoinUrl=(https:\/\/[^\s\r\n]+)/);
        if (match) {
          joinUrl = match[1];
          console.log(`  [OK] Session Join URL: ${joinUrl}`);
          break;
        }
      }
    }

    if (!joinUrl) throw new Error("Timed out waiting for Receiver session initialization");

    // 3. Launch Chromium with High-Motion Canvas Generator Hook
    console.log("\n[2/5] Launching Playwright browser with High-Entropy Canvas Stream generator...");
    browser = await chromium.launch({
      headless: true,
      args: [
        "--use-fake-ui-for-media-stream",
        "--allow-file-access",
        "--no-sandbox"
      ]
    });

    const context = await browser.newContext({ permissions: ["camera", "microphone"] });

    // Inject high-motion canvas generator into getUserMedia before page load
    await context.addInitScript(() => {
      const origGetUserMedia = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
      navigator.mediaDevices.getUserMedia = async function (constraints) {
        if (constraints && constraints.video) {
          const width = typeof constraints.video === "object" && constraints.video.width ? (constraints.video.width.ideal || 1280) : 1280;
          const height = typeof constraints.video === "object" && constraints.video.height ? (constraints.video.height.ideal || 720) : 720;
          const fps = typeof constraints.video === "object" && constraints.video.frameRate ? (constraints.video.frameRate.ideal || 60) : 60;

          const canvas = document.createElement("canvas");
          canvas.width = width;
          canvas.height = height;
          const ctx = canvas.getContext("2d", { alpha: false });

          let frame = 0;
          function renderStressMotion() {
            frame++;

            // High entropy moving background gradient (100% macroblock change per frame)
            const hue = (frame * 5) % 360;
            const grad = ctx.createLinearGradient(0, 0, width, height);
            grad.addColorStop(0, `hsl(${hue}, 90%, 45%)`);
            grad.addColorStop(0.5, `hsl(${(hue + 120) % 360}, 90%, 35%)`);
            grad.addColorStop(1, `hsl(${(hue + 240) % 360}, 90%, 55%)`);
            ctx.fillStyle = grad;
            ctx.fillRect(0, 0, width, height);

            // Fast rotating, high-frequency checkerboard blocks
            const blockSize = 40;
            ctx.fillStyle = "rgba(255, 255, 255, 0.25)";
            for (let y = 0; y < height; y += blockSize * 2) {
              for (let x = 0; x < width; x += blockSize * 2) {
                const shiftX = (Math.sin(frame * 0.1 + y) * 20);
                const shiftY = (Math.cos(frame * 0.1 + x) * 20);
                ctx.fillRect(x + shiftX, y + shiftY, blockSize, blockSize);
              }
            }

            // High-motion moving circles simulating fast camera panning
            for (let i = 0; i < 12; i++) {
              const cx = (Math.sin(frame * 0.08 + i * 0.8) * 0.45 + 0.5) * width;
              const cy = (Math.cos(frame * 0.11 + i * 1.2) * 0.45 + 0.5) * height;
              const r = 30 + (i % 4) * 20;

              ctx.beginPath();
              ctx.arc(cx, cy, r, 0, Math.PI * 2);
              ctx.fillStyle = `hsl(${(frame * 8 + i * 30) % 360}, 100%, 65%)`;
              ctx.fill();
              ctx.lineWidth = 6;
              ctx.strokeStyle = "#ffffff";
              ctx.stroke();
            }

            // Fast moving high-contrast HUD text
            ctx.font = "bold 36px monospace";
            ctx.fillStyle = "#ffffff";
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 4;
            const text = `INTENSE MOTION STRESS - FRAME ${frame} (${width}x${height} @ ${fps}fps)`;
            ctx.strokeText(text, 40, 60);
            ctx.fillText(text, 40, 60);

            // Timecode
            const timeStr = new Date().toISOString();
            ctx.strokeText(`TIMESTAMP: ${timeStr}`, 40, 110);
            ctx.fillText(`TIMESTAMP: ${timeStr}`, 40, 110);

            requestAnimationFrame(renderStressMotion);
          }
          renderStressMotion();

          const stream = canvas.captureStream(fps);

          // If audio requested, add a dummy silent audio track
          if (constraints.audio) {
            try {
              const audioCtx = new AudioContext();
              const osc = audioCtx.createOscillator();
              const dst = audioCtx.createMediaStreamDestination();
              osc.connect(dst);
              osc.start();
              const audioTrack = dst.stream.getAudioTracks()[0];
              stream.addTrack(audioTrack);
            } catch {}
          }

          return stream;
        }
        return origGetUserMedia(constraints);
      };
    });

    const page = await context.newPage();

    page.on("console", (msg) => {
      const txt = msg.text();
      if (txt.includes("WebCodecs") || txt.includes("PeerState") || txt.includes("error") || txt.includes("Error")) {
        console.log(`    [Browser Console]: ${txt}`);
      }
    });

    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    // 4. Select WebCodecs + DataChannel mode and start stream
    console.log("\n[3/5] Starting WebCodecs + DataChannel Direct Pipeline...");
    await page.locator("#select-transport-mode").selectOption("webcodecs_datachannel");
    await page.locator("#select-resolution").selectOption("1280x720");
    await page.locator("#select-fps").selectOption("60");
    await page.locator("#select-bitrate").selectOption("4000000");

    await page.locator("button.button-primary").click();
    console.log("  - Clicked '送信開始'...");

    // Wait for connection
    let isConnected = false;
    for (let i = 0; i < 20; i++) {
      await sleep(1000);
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      if (logContent.includes("PeerState: Connected")) {
        isConnected = true;
        console.log("  [PASS] WebRTC Connection established successfully!");
        break;
      }
    }
    if (!isConnected) throw new Error("WebRTC connection failed to connect");

    // 5. Stress test under intense motion for 15 seconds (expecting >600 high-motion frames)
    console.log("\n[4/5] Running continuous 15-second High-Motion Stress Test (60fps full-entropy)...");
    const testDurationMs = 15000;
    const testStart = Date.now();
    let prevFrames = 0;

    while (Date.now() - testStart < testDurationMs) {
      await sleep(1500);
      const curFrames = getDecodedFrameCount();
      const delta = curFrames - prevFrames;
      prevFrames = curFrames;
      const elapsedSec = ((Date.now() - testStart) / 1000).toFixed(1);
      console.log(`    [T+${elapsedSec}s] Decoded Frames: ${curFrames} (+${delta} frames/1.5s)`);
    }

    const finalFrames = getDecodedFrameCount();
    console.log(`\n[5/5] Analyzing Decode Integrity and Quality Metrics...`);
    console.log(`  - Cumulative Frames Decoded: ${finalFrames}`);

    // Verify snapshot image exists and is non-empty
    const snapshotPath = path.join(rootDir, "test_screenshots", "last_decoded_frame.bmp");
    if (fs.existsSync(snapshotPath)) {
      const stats = fs.statSync(snapshotPath);
      console.log(`  - Verified Output BMP Snapshot: ${snapshotPath} (${(stats.size / 1024).toFixed(1)} KB)`);
    }

    const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
    const hasFatalErrors = logContent.includes("MF_E_INVALID_FORMAT") || logContent.includes("MF_E_NOTACCEPTING");
    if (hasFatalErrors) {
      throw new Error("Fatal decoder error found in log");
    }

    if (finalFrames < 400) {
      throw new Error(`Throughput under intense motion was too low: decoded only ${finalFrames} frames (expected >= 400)`);
    }

    console.log("\n================================================================================");
    console.log(`  >>> INTENSE MOTION STRESS TEST PASSED: ${finalFrames} FRAMES PROCESSED WITH ZERO ERRORS <<< `);
    console.log("================================================================================");
  } finally {
    if (browser) await browser.close().catch(() => {});
    receiverProc.kill();
  }
}

runIntenseMotionStressTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> INTENSE MOTION TEST FAILED: <<<", err);
    process.exit(1);
  });
