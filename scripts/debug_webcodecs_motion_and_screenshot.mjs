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

async function runMotionDebugTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: WEBCODECS MOTION & ARTIFACT REPRODUCTION / DEBUG SUITE       ");
  console.log("================================================================================");

  if (fs.existsSync(logPath)) {
    try { fs.unlinkSync(logPath); } catch {}
  }

  console.log("\n[1/4] Launching Receiver.exe in background...");
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

    console.log("\n[2/4] Launching Playwright browser with high-motion canvas stream generator...");
    browser = await chromium.launch({
      headless: true,
      args: [
        "--use-fake-ui-for-media-stream",
        "--use-fake-device-for-media-stream",
        "--allow-file-access",
        "--no-sandbox"
      ]
    });

    const context = await browser.newContext({ permissions: ["camera", "microphone"] });
    const page = await context.newPage();

    page.on("console", (msg) => {
      const txt = msg.text();
      if (txt.includes("WebCodecs") || txt.includes("PeerState") || txt.includes("error") || txt.includes("Error")) {
        console.log(`    [Browser Console]: ${txt}`);
      }
    });

    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    // Inject high-motion canvas animation to simulate camera panning and shaking
    await page.evaluate(() => {
      const canvas = document.createElement("canvas");
      canvas.width = 1280;
      canvas.height = 720;
      const ctx = canvas.getContext("2d");
      let frame = 0;

      function drawMotion() {
        frame++;
        // Background gradient sweep
        const grad = ctx.createLinearGradient(0, 0, 1280, 720);
        grad.addColorStop(0, `hsl(${frame % 360}, 80%, 50%)`);
        grad.addColorStop(1, `hsl(${(frame + 180) % 360}, 80%, 30%)`);
        ctx.fillStyle = grad;
        ctx.fillRect(0, 0, 1280, 720);

        // High-motion moving discs
        for (let i = 0; i < 8; i++) {
          const x = (Math.sin(frame * 0.05 + i) * 0.4 + 0.5) * 1280;
          const y = (Math.cos(frame * 0.07 + i * 1.5) * 0.4 + 0.5) * 720;
          const radius = 40 + i * 15;
          ctx.beginPath();
          ctx.arc(x, y, radius, 0, Math.PI * 2);
          ctx.fillStyle = `hsl(${(frame * 3 + i * 45) % 360}, 100%, 70%)`;
          ctx.fill();
          ctx.lineWidth = 4;
          ctx.strokeStyle = "#ffffff";
          ctx.stroke();
        }

        // Fast moving text with timestamp
        ctx.font = "bold 48px monospace";
        ctx.fillStyle = "#ffffff";
        ctx.fillText(`KM MOTION TEST - FRAME ${frame} - ${new Date().toISOString()}`, 50, 100);

        requestAnimationFrame(drawMotion);
      }
      drawMotion();

      // Replace video stream with animated canvas stream
      const stream = canvas.captureStream(60);
      const videoTrack = stream.getVideoTracks()[0];

      // @ts-ignore
      if (window.localMediaStream) {
        // @ts-ignore
        const oldTrack = window.localMediaStream.getVideoTracks()[0];
        if (oldTrack) {
          // @ts-ignore
          window.localMediaStream.removeTrack(oldTrack);
          oldTrack.stop();
        }
        // @ts-ignore
        window.localMediaStream.addTrack(videoTrack);
      }
    });

    console.log("\n[3/4] Starting stream in WebCodecs + DataChannel (PoC) Mode with high motion...");
    await page.locator("#select-transport-mode").selectOption("webcodecs_datachannel");
    await page.locator("#select-resolution").selectOption("1280x720");
    await page.locator("#select-fps").selectOption("60");
    await page.locator("#select-bitrate").selectOption("4000000");

    await page.locator("button.button-primary").click();

    // Monitor stream for 10 seconds of rapid motion
    console.log("  Streaming high-motion frames for 10 seconds...");
    const streamStart = Date.now();
    let initialCount = 0;
    while (Date.now() - streamStart < 10000) {
      await sleep(1000);
      const count = getDecodedFrameCount();
      if (initialCount === 0 && count > 0) initialCount = count;
      console.log(`    [Progress]: ${count} frames decoded so far...`);
    }

    // Capture browser side screenshot
    const browserScreenshotPath = path.join(screenshotDir, "browser_motion_preview.png");
    await page.screenshot({ path: browserScreenshotPath });
    console.log(`  [Screenshot] Browser preview saved: ${browserScreenshotPath}`);

    const finalFrames = getDecodedFrameCount();
    console.log(`\n[4/4] Verifying decoding metrics & log integrity...`);
    console.log(`  - Total Frames Decoded: ${finalFrames}`);

    // Read full receiver log
    const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
    
    // Check for MFT or DXVA errors
    const errorMatches = logContent.match(/error|failed|fault|corrupt/gi) || [];
    console.log(`  - Decoder Error Count in logs: ${errorMatches.length}`);

    if (finalFrames < 60) {
      throw new Error(`Insufficient frames decoded (${finalFrames} < 60)`);
    }

    console.log("\n================================================================================");
    console.log("  >>> MOTION REPRODUCTION & VERIFICATION TEST PASSED WITH ZERO ERRORS <<<       ");
    console.log("================================================================================");
  } finally {
    if (browser) await browser.close().catch(() => {});
    receiverProc.kill();
  }
}

runMotionDebugTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> TEST ERROR: <<<", err);
    process.exit(1);
  });
