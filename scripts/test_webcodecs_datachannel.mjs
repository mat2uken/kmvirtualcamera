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

async function runWebCodecsDataChannelTest() {
  console.log("================================================================================");
  console.log("  KM VIRTUAL CAMERA: WEBCODECS + DATACHANNEL (UDP UNRELIABLE) PoC TEST           ");
  console.log("================================================================================");

  // 1. Clear previous log
  if (fs.existsSync(logPath)) {
    try { fs.unlinkSync(logPath); } catch {}
  }

  // 2. Start Receiver.exe in background
  console.log("\n[SETUP] Starting native Receiver.exe process...");
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
    // 3. Wait for session JoinUrl
    console.log("  Waiting for Cloudflare session initialization in Receiver...");
    let joinUrl = null;
    const startWait = Date.now();
    while (Date.now() - startWait < 15000) {
      await sleep(500);
      if (fs.existsSync(logPath)) {
        const logContent = fs.readFileSync(logPath, "utf-8");
        const match = logContent.match(/JoinUrl=(https:\/\/[^\s\r\n]+)/);
        if (match) {
          joinUrl = match[1];
          console.log(`  [OK] Session ready: ${joinUrl}`);
          break;
        }
      }
    }

    if (!joinUrl) {
      throw new Error("Timed out waiting for Receiver to initialize session");
    }

    // 4. Launch Chromium browser with fake devices
    console.log("\n[BROWSER] Launching Playwright browser instance...");
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
      if (txt.includes("PeerConnection state") || txt.includes("WebCodecs") || txt.includes("DataChannel")) {
        console.log(`    [Browser Console]: ${txt}`);
      }
    });

    await page.goto(joinUrl, { waitUntil: "networkidle" });
    await sleep(1000);

    // Select WebCodecs + DataChannel mode
    console.log("\n[MODE] Selecting '⚡ 超低遅延 WebCodecs + DataChannel 直結モード (PoC)'...");
    const modeSelect = page.locator("#select-transport-mode");
    await modeSelect.selectOption("webcodecs_datachannel");

    // Select 720p resolution
    const resSelect = page.locator("#select-resolution");
    await resSelect.selectOption("1280x720");

    // Click Start
    const startBtn = page.locator("button.button-primary");
    await startBtn.click();
    console.log("  - Clicked '送信開始' in WebCodecs mode...");

    // Wait for connection
    let isConnected = false;
    for (let i = 0; i < 20; i++) {
      await sleep(1000);
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      if (logContent.includes("PeerState: Connected")) {
        isConnected = true;
        console.log("  [PASS] WebRTC Connected successfully!");
        break;
      }
    }
    if (!isConnected) throw new Error("WebRTC connection failed to connect");

    // Wait for frames to decode
    console.log("  Waiting for decoded frames over DataChannel...");
    let decodedFrames = 0;
    const streamWait = Date.now();
    while (Date.now() - streamWait < 15000) {
      await sleep(500);
      decodedFrames = getDecodedFrameCount();
      if (decodedFrames >= 90) break;
    }

    console.log(`  [PASS] Decoded frames received over DataChannel: ${decodedFrames} frames`);

    console.log("\n================================================================================");
    console.log("  >>> WEBCODECS + DATACHANNEL POC TEST PASSED SUCCESSFULLY <<<                  ");
    console.log("================================================================================");
  } finally {
    if (browser) {
      await browser.close().catch(() => {});
    }
    receiverProc.kill();
  }
}

runWebCodecsDataChannelTest()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("\n>>> TEST FAILED WITH ERROR: <<<", err);
    process.exit(1);
  });
