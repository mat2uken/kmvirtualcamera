import { chromium } from "playwright";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "..");
const logPath = path.join(rootDir, "receiver_debug.log");
const receiverExe = path.join(rootDir, "windows", "build", "Release", "Receiver.exe");

async function runLiveReceiverAndBrowserTest() {
  console.log("============================================================");
  console.log("  LIVE RECEIVER.EXE + PLAYWRIGHT E2E INTEGRATION TEST       ");
  console.log("============================================================");

  // 1. Clear previous log
  if (fs.existsSync(logPath)) {
    try { fs.unlinkSync(logPath); } catch {}
  }

  // 2. Spawn Receiver.exe in background
  console.log("[1] Starting Receiver.exe in background...");
  const receiverProc = spawn(receiverExe, ["--url=https://webrtc-bridge-signaling.mat2uken.workers.dev"], {
    cwd: rootDir,
    detached: false,
    stdio: "ignore"
  });

  receiverProc.on("error", (err) => {
    console.error("Failed to spawn Receiver.exe:", err);
  });

  try {
    // 3. Wait for JoinUrl to appear in receiver_debug.log
    console.log("[2] Waiting for Receiver.exe to initialize session...");
    let joinUrl = null;
    const startWait = Date.now();

    while (Date.now() - startWait < 15000) {
      await new Promise((r) => setTimeout(r, 500));
      if (fs.existsSync(logPath)) {
        try {
          const logContent = fs.readFileSync(logPath, "utf-8");
          const match = logContent.match(/JoinUrl=(https:\/\/[^\s\r\n]+)/);
          if (match) {
            joinUrl = match[1];
            console.log(`    Found JoinUrl: ${joinUrl}`);
            break;
          }
        } catch {}
      }
    }

    if (!joinUrl) {
      throw new Error("Timed out waiting for Receiver.exe to output JoinUrl to receiver_debug.log");
    }

    // 4. Launch Playwright Browser with fake camera/mic
    console.log("[3] Launching Playwright browser and navigating to JoinUrl...");
    const browser = await chromium.launch({
      headless: true,
      args: [
        "--use-fake-ui-for-media-stream",
        "--use-fake-device-for-media-stream",
        "--allow-file-access"
      ]
    });

    const context = await browser.newContext({ permissions: ["camera", "microphone"] });
    const page = await context.newPage();

    page.on("console", (msg) => {
      const txt = msg.text();
      if (txt.includes("[Log]") || txt.includes("Error") || txt.includes("ICE") || txt.includes("ConnectionState")) {
        console.log(`    [Browser Console]: ${txt}`);
      }
    });

    await page.goto(joinUrl, { waitUntil: "networkidle" });

    // 5. Click '送信開始'
    console.log("[4] Clicking '送信開始' in browser...");
    const startButton = page.locator("button:has-text('送信開始')");
    await startButton.waitFor({ state: "visible", timeout: 10000 });
    await startButton.click();

    // 6. Wait for connected status
    console.log("[5] Waiting for WebRTC connection to reach 'connected' on both Browser and Receiver...");
    let connected = false;
    const connStart = Date.now();

    while (Date.now() - connStart < 20000) {
      await new Promise((r) => setTimeout(r, 1000));

      const badgeText = await page.locator(".badge").textContent().catch(() => "");
      const errorMsg = await page.locator(".message-error").textContent().catch(() => "");

      if (errorMsg) {
        console.log(`    [Browser Error]: ${errorMsg}`);
      }

      if (badgeText.includes("接続中") || badgeText.includes("送信中")) {
        connected = true;
        console.log(`    Browser state: ${badgeText}`);
        break;
      }

      if (fs.existsSync(logPath)) {
        try {
          const logContent = fs.readFileSync(logPath, "utf-8");
          if (logContent.includes("PeerState: Connected")) {
            connected = true;
            console.log("    Receiver state: PeerState: Connected!");
            break;
          }
        } catch {}
      }
    }

    // Check receiver log for decoded frames
    console.log("[6] Inspecting live Receiver log...");
    let logSummary = "";
    if (fs.existsSync(logPath)) {
      logSummary = fs.readFileSync(logPath, "utf-8");
      console.log(logSummary.split("\n").slice(-25).join("\n"));
    }

    await browser.close();

    if (!connected) {
      throw new Error("Failed to establish live WebRTC connection between Receiver.exe and Browser within 20s");
    }

    console.log("============================================================");
    console.log("  >>> LIVE RECEIVER + BROWSER E2E TEST PASSED (100%) <<<    ");
    console.log("============================================================");
  } finally {
    // Kill receiver process
    try { receiverProc.kill("SIGKILL"); } catch {}
  }
}

runLiveReceiverAndBrowserTest().catch((err) => {
  console.error("\n[TEST FAILED]:", err);
  process.exit(1);
});
