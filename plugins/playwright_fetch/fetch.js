const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

function usage() {
  console.error("Usage: node fetch.js <url> --out <path> [--state <path>] [--timeout <ms>] [--wait <selector>]");
  console.error("                     [--headed] [--pause]");
}

function parseArgs(argv) {
  const args = {
    url: "",
    out: "",
    state: "",
    timeout: 60000,
    wait: "",
    headed: false,
    pause: false,
  };
  const positionals = [];
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--out") {
      args.out = argv[++i] || "";
    } else if (arg === "--state") {
      args.state = argv[++i] || "";
    } else if (arg === "--timeout") {
      const raw = argv[++i];
      const parsed = Number(raw);
      if (!Number.isFinite(parsed) || parsed <= 0) {
        throw new Error(`Invalid --timeout value: ${raw}`);
      }
      args.timeout = Math.floor(parsed);
    } else if (arg === "--wait") {
      args.wait = argv[++i] || "";
    } else if (arg === "--headed" || arg === "--headful") {
      args.headed = true;
    } else if (arg === "--pause") {
      args.pause = true;
    } else if (arg && arg.startsWith("--")) {
      throw new Error(`Unknown option: ${arg}`);
    } else if (arg) {
      positionals.push(arg);
    }
  }
  if (positionals.length > 0) {
    args.url = positionals[0];
  }
  if (args.pause) {
    args.headed = true;
  }
  return args;
}

function waitForEnter(message) {
  return new Promise((resolve) => {
    if (message) {
      process.stderr.write(message);
    }
    if (!process.stdin.isTTY) {
      throw new Error("--pause requires an interactive terminal");
    }
    process.stdin.setEncoding("utf8");
    process.stdin.resume();
    process.stdin.once("data", () => {
      process.stdin.pause();
      resolve();
    });
  });
}

async function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
  } catch (err) {
    console.error(err.message || String(err));
    usage();
    process.exit(1);
  }

  if (!args.url || !args.out) {
    usage();
    process.exit(1);
  }

  fs.mkdirSync(path.dirname(args.out), { recursive: true });
  if (args.state) {
    fs.mkdirSync(path.dirname(args.state), { recursive: true });
  }

  const browser = await chromium.launch({ headless: !args.headed });
  const context = args.state && fs.existsSync(args.state)
    ? await browser.newContext({ storageState: args.state })
    : await browser.newContext();
  const page = await context.newPage();

  await page.goto(args.url, { waitUntil: "networkidle", timeout: args.timeout });
  if (args.pause) {
    await page.bringToFront();
    await waitForEnter("Solve any verification in the browser, then press Enter to continue...\n");
  }
  if (args.wait) {
    await page.waitForSelector(args.wait, { timeout: args.timeout });
  }

  const html = await page.content();

  fs.writeFileSync(args.out, html, "utf8");
  if (args.state) {
    await context.storageState({ path: args.state });
  }
  await browser.close();
}

main().catch((err) => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
