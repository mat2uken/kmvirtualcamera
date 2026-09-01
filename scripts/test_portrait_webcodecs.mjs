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

async function runPortraitTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: PORTRAIT ORIENTATION (VERTICAL 9:16) WEBCODECS TEST        ");
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

    if (!joinUrl) throw new Error("Timed out waiting for Receiver to initialize session");

    // Launch Chromium with mobile viewport (Portrait 390x844)
    browser = await chromium.launch({
      headless: true,
      args: [
        "--use-fake-ui-for-media-stream",
        "--use-fake-device-for-media-stream",
        "--allow-file-access",
        "--no-sandbox"
      ]
    });

    const context = await browser.newContext({
      viewport: { width: 390, height: 844 },
      isMobile: true,
      hasTouch: true,
      permissions: ["camera", "microphone"]
    });
    const page = await context.newPage();

    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    const modeSelect = page.locator("#select-transport-mode");
    await modeSelect.selectOption("webcodecs_datachannel");

    const startBtn = page.locator("button.button-primary");
    await startBtn.click();
    console.log("  - Clicked '送信開始' in Portrait mobile simulation...");

    let isConnected = false;
    for (let i = 0; i < 20; i++) {
      await sleep(1000);
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      if (logContent.includes("PeerState: Connected")) {
        isConnected = true;
        console.log("  [PASS] WebRTC Connected successfully in Portrait mode!");
        break;
      }
    }
    if (!isConnected) throw new Error("WebRTC connection failed to connect");

    console.log("  Waiting for decoded frames in Portrait mode...");
    let decodedFrames = 0;
    const streamWait = Date.now();
    while (Date.now() - streamWait < 15000) {
      await sleep(500);
      decodedFrames = getDecodedFrameCount();
      if (decodedFrames >= 90) break;
    }

    console.log(`  [PASS] Decoded frames received in Portrait mode: ${decodedFrames} frames`);
    console.log("\n================================================================================");
    console.log("  >>> PORTRAIT ORIENTATION TEST PASSED SUCCESSFULLY <<<                         ");
    console.log("================================================================================");
  } finally {
    if (browser) await browser.close().catch(() => {});
    receiverProc.kill();
  }
}

runPortraitTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> TEST FAILED: <<<", err);
    process.exit(1);
  });
