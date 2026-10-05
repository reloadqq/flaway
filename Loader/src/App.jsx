import { useEffect, useRef, useState } from "react";

import Boot from "./components/Boot.jsx";
import TitleBar from "./components/TitleBar.jsx";
import Sidebar from "./components/Sidebar.jsx";
import Toast from "./components/Toast.jsx";
import InjectOverlay from "./components/InjectOverlay.jsx";
import HomePage from "./pages/HomePage.jsx";
import SubscriptionPage from "./pages/SubscriptionPage.jsx";
import SettingsPage from "./pages/SettingsPage.jsx";

import useParallax from "./lib/useParallax.js";
import { THEMES } from "./lib/themes.js";
import { initNative, minimizeWindow, exitApp } from "./lib/native.js";

const MESSAGES = [
  "initializing...",
  "loading modules...",
  "checking subscription...",
  "preparing environment...",
  "loading flaway core...",
  "injecting...",
  "almost done..."
];

const BOOT_MS = 1800;

export default function App() {
  const [booted, setBooted] = useState(false);
  const [page, setPage] = useState("home");
  const [version, setVersion] = useState("DLC");
  const [themeId, setThemeId] = useState("violet");
  const [glass, setGlass] = useState(true);
  const [parallax, setParallax] = useState(true);
  const [minimized, setMinimized] = useState(false);
  const [closing, setClosing] = useState(false);
  const [closed, setClosed] = useState(false);
  const [injecting, setInjecting] = useState(false);
  const [injectText, setInjectText] = useState(MESSAGES[0]);
  const [toast, setToast] = useState({ visible: false, title: "", text: "" });

  const toastTimer = useRef(null);
  const injectTimer = useRef(null);
  const injectDelay = useRef(null);

  useParallax(parallax && !closed);

  useEffect(() => {
    initNative();
    const timer = setTimeout(() => setBooted(true), BOOT_MS);
    return () => clearTimeout(timer);
  }, []);

  useEffect(() => {
    const theme = THEMES.find((item) => item.id === themeId) || THEMES[0];
    const root = document.documentElement;
    root.style.setProperty("--accent", theme.accent);
    root.style.setProperty("--accent2", theme.accent2);
    root.style.setProperty("--rgb", theme.rgb);
  }, [themeId]);

  useEffect(() => {
    document.documentElement.classList.toggle("no-glass", !glass);
  }, [glass]);

  useEffect(
    () => () => {
      clearTimeout(toastTimer.current);
      clearInterval(injectTimer.current);
      clearTimeout(injectDelay.current);
    },
    []
  );

  const showToast = (title, text) => {
    setToast({ visible: true, title, text });
    clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(
      () => setToast((prev) => ({ ...prev, visible: false })),
      3000
    );
  };

  const stopInject = () => {
    clearInterval(injectTimer.current);
    clearTimeout(injectDelay.current);
    injectTimer.current = null;
    injectDelay.current = null;
    setInjecting(false);
  };

  const startInject = () => {
    clearInterval(injectTimer.current);
    clearTimeout(injectDelay.current);

    setInjecting(true);
    setInjectText(MESSAGES[0]);

    let index = 0;

    injectTimer.current = setInterval(() => {
      index++;

      if (index >= MESSAGES.length) {
        clearInterval(injectTimer.current);
        injectTimer.current = null;
        setInjectText("completed successfully.");

        injectDelay.current = setTimeout(() => {
          setInjecting(false);
          showToast("Injection complete", version + " is ready.");
        }, 900);

        return;
      }

      setInjectText(MESSAGES[index]);
    }, 700);
  };

  const handleMinimize = () => {
    setMinimized(true);
    minimizeWindow();
  };

  const handleRestore = () => setMinimized(false);

  const handleClose = () => {
    if (closing || closed) return;
    setClosing(true);
    exitApp(520);
    setTimeout(() => setClosed(true), 350);
  };

  useEffect(() => {
    const onKey = (event) => {
      if (event.key === "Escape" && injecting) stopInject();
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [injecting]);

  const appStyle = closing
    ? {
        opacity: "0",
        transform: "scale(.96)",
        transition: "opacity .35s ease, transform .35s ease"
      }
    : undefined;

  return (
    <>
      <div className="background"></div>
      <div className="blob one"></div>
      <div className="blob two"></div>
      <div className="noise"></div>

      <Boot hidden={booted} />

      {!closed && (
        <div
          className={`app${minimized ? " hidden-window" : ""}`}
          id="app"
          style={appStyle}
        >
          <div className="shell">
            <div className="drag-strip" id="dragStrip"></div>

            <TitleBar onMinimize={handleMinimize} onClose={handleClose} />

            <Sidebar
              page={page}
              setPage={setPage}
              onDownload={() =>
                showToast("Download", "Latest flaway build is ready.")
              }
            />

            <main className="content">
              {page === "home" && (
                <HomePage
                  version={version}
                  setVersion={setVersion}
                  onInject={startInject}
                />
              )}

              {page === "subscription" && (
                <SubscriptionPage version={version} setVersion={setVersion} />
              )}

              {page === "settings" && (
                <SettingsPage
                  themeId={themeId}
                  setTheme={setThemeId}
                  glass={glass}
                  setGlass={setGlass}
                  parallax={parallax}
                  setParallax={setParallax}
                />
              )}
            </main>
          </div>
        </div>
      )}

      <button
        type="button"
        id="restore"
        className={`restore-window${minimized && !closed ? " visible" : ""}`}
        onClick={handleRestore}
      >
        F
      </button>

      <Toast toast={toast} />

      <InjectOverlay active={injecting} text={injectText} />

      {closed && <div className="closed-screen">flaway closed</div>}
    </>
  );
}
