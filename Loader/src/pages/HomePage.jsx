import VersionCards from "../components/VersionCards.jsx";

export default function HomePage({ version, setVersion, onInject }) {
  return (
    <section className="page" id="home">
      <header className="header">
        <div>
          <div className="eyebrow">FLaway / HOME</div>
          <h1 className="title">Welcome back.</h1>
          <p className="subtitle">Choose your version and continue.</p>
        </div>

        <div className="status">
          <i className="online"></i>
          Online
        </div>
      </header>

      <div className="hero">
        <div className="hero-content">
          <div className="eyebrow">PREMIUM LOADER</div>
          <h2>
            FLaway <span>DLC</span>
          </h2>
          <p>
            Your workspace, your control. Select a version below and launch the
            loader with a clean liquid-glass interface.
          </p>
        </div>
      </div>

      <div className="section-title">Available versions</div>

      <VersionCards version={version} onSelect={setVersion} />

      <div className="action-row">
        <div className="selected-info">
          <div className="selected-dot"></div>
          <div>
            <strong id="selectedText" className="text-swap" key={version}>
              {version} selected
            </strong>
            <span>Ready to inject</span>
          </div>
        </div>

        <button type="button" id="inject" className="inject" onClick={onInject}>
          Inject
        </button>
      </div>
    </section>
  );
}
