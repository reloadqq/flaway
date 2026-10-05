import { THEMES } from "../lib/themes.js";

export default function SettingsPage({
  themeId,
  setTheme,
  glass,
  setGlass,
  parallax,
  setParallax
}) {
  return (
    <section className="page" id="settings">
      <header className="header">
        <div>
          <div className="eyebrow">FLaway / SETTINGS</div>
          <h1 className="title">Settings</h1>
          <p className="subtitle">Customize your interface.</p>
        </div>
      </header>

      <div className="settings-grid">
        <div className="setting-card">
          <h3>Accent theme</h3>
          <p>Change the primary interface color.</p>

          <div className="themes">
            {THEMES.map((theme) => (
              <div
                key={theme.id}
                className={`theme${themeId === theme.id ? " active" : ""}`}
                style={{
                  background: theme.accent,
                  "--theme": theme.accent
                }}
                onClick={() => setTheme(theme.id)}
              ></div>
            ))}
          </div>
        </div>

        <div className="setting-card">
          <h3>Interface</h3>
          <p>Liquid Glass visual effects.</p>

          <div className="toggle-row">
            <span>Liquid Glass</span>
            <div
              className={`toggle${glass ? " on" : ""}`}
              onClick={() => setGlass(!glass)}
            >
              <span></span>
            </div>
          </div>

          <div className="toggle-row">
            <span>Parallax</span>
            <div
              className={`toggle${parallax ? " on" : ""}`}
              onClick={() => setParallax(!parallax)}
            >
              <span></span>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
