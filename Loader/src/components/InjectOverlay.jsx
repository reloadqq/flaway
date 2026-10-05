export default function InjectOverlay({ active, text }) {
  return (
    <div className={`inject-overlay${active ? " active" : ""}`} id="injectOverlay">
      <div className="inject-box">
        <div className="inject-spinner"></div>
        <h2>Injecting DLC</h2>
        <p className="inject-text-change" id="injectText" key={text}>
          {text}
        </p>
      </div>
    </div>
  );
}
