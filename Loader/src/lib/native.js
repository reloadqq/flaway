const bridge = () =>
  typeof window !== "undefined" ? window.Neutralino : undefined;

// NL_* globals are injected by the Neutralino runtime before any page
// script runs, so they are a reliable "am I inside the app?" check
export const isNative = () => {
  const n = bridge();
  if (!n || typeof n.init !== "function") return false;
  return Boolean(
    window.NL_APPID || window.NL_PORT || window.NL_ARGS || window.NL_TOKEN
  );
};

const safe = (fn) => {
  try {
    const result = fn();
    if (result && typeof result.catch === "function") result.catch(() => {});
    return result;
  } catch {
    return undefined;
  }
};

export function initNative({ onRestore } = {}) {
  if (!isNative()) return;
  safe(() => bridge().init());
  safe(() =>
    bridge().events.on("windowClose", () => safe(() => bridge().app.exit()))
  );
  // Restoring from the taskbar must clear React's `minimized` state, otherwise
  // `.app.hidden-window` keeps the whole UI at opacity 0 with no way back.
  safe(() => bridge().events.on("windowRestore", () => onRestore?.()));
  safe(() => bridge().events.on("windowFocus", () => onRestore?.()));
  safe(() => bridge().window.setDraggableRegion("dragStrip"));
}

export function minimizeWindow() {
  if (!isNative()) return false;
  safe(() => bridge().window.minimize());
  return true;
}

export function exitApp(delay = 450) {
  if (!isNative()) return false;
  setTimeout(() => safe(() => bridge().app.exit()), delay);
  return true;
}
