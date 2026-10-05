import VersionCards from "../components/VersionCards.jsx";

export default function SubscriptionPage({ version, setVersion }) {
  return (
    <section className="page" id="subscription">
      <header className="header">
        <div>
          <div className="eyebrow">FLaway / SUBSCRIPTION</div>
          <h1 className="title">Subscription</h1>
          <p className="subtitle">Your current access and features.</p>
        </div>
      </header>

      <div className="plan">
        <div className="plan-top">
          <div>
            <div className="eyebrow">CURRENT PLAN</div>
            <div className="plan-name">flaway DLC</div>
          </div>

          <div className="badge">PREMIUM</div>
        </div>

        <div className="features">
          <div className="feature">
            <b>∞</b>
            Unlimited access
          </div>

          <div className="feature">
            <b>⚡</b>
            Fast injection
          </div>

          <div className="feature">
            <b>◈</b>
            Premium builds
          </div>
        </div>
      </div>

      <div className="section-title">Select version</div>

      <VersionCards version={version} onSelect={setVersion} />
    </section>
  );
}
