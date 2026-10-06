import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

// Resolve against this file, not process.cwd(): running the script from any
// other directory would otherwise look for dist/ in the wrong place.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dist = path.join(root, "dist");
const binName = "flaway-dlc";
const stage = path.join(dist, binName);

if (!fs.existsSync(stage)) {
  throw new Error("dist/" + binName + " not found, run `neu build --release` first");
}

const RESOURCES = "resources.neu";

const targets = [
  { platform: "win_x64", file: `${binName}-win_x64.exe`, archive: `${binName}-win_x64-portable.zip` },
  { platform: "linux_x64", file: `${binName}-linux_x64`, archive: `${binName}-linux_x64-portable.tar.gz` }
];

for (const target of targets) {
  const src = path.join(stage, target.file);
  if (!fs.existsSync(src)) throw new Error("missing binary: " + target.file);

  const out = path.join(dist, target.archive);
  fs.rmSync(out, { force: true });

  if (target.archive.endsWith(".zip")) {
    execFileSync("zip", ["-j", "-9", out, src, path.join(stage, RESOURCES)], { stdio: "inherit" });
  } else {
    execFileSync("tar", ["-czf", out, "-C", stage, target.file, RESOURCES], { stdio: "inherit" });
  }

  console.log(`packed ${target.archive}`);
}

// ready to run linux folder: binary + resources.neu (double click to start)
const readyDir = path.join(dist, `${binName}-linux_x64`);
fs.rmSync(readyDir, { recursive: true, force: true });
fs.mkdirSync(readyDir, { recursive: true });
fs.copyFileSync(path.join(stage, `${binName}-linux_x64`), path.join(readyDir, `${binName}-linux_x64`));
fs.copyFileSync(path.join(stage, RESOURCES), path.join(readyDir, RESOURCES));
fs.chmodSync(path.join(readyDir, `${binName}-linux_x64`), 0o755);

// drop the multi platform staging folder and the all-in-one zip
fs.rmSync(stage, { recursive: true, force: true });
fs.rmSync(path.join(dist, `${binName}-release.zip`), { force: true });

for (const target of targets) {
  const stat = fs.statSync(path.join(dist, target.archive));
  console.log(`${target.archive}: ${(stat.size / 1024).toFixed(1)} KiB`);
}
console.log(`ready folder: dist/${binName}-linux_x64/ (2 files)`);
