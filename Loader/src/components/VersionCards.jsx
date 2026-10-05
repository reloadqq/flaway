const CARDS = [
  {
    id: "DLC",
    tag: "STABLE",
    title: "DLC",
    desc: "Recommended version",
    bg: "/dlc.jpg"
  },
  {
    id: "Beta",
    tag: "EXPERIMENTAL",
    title: "Beta",
    desc: "New features & testing",
    bg: "/beta.jpg"
  }
];

export default function VersionCards({ version, onSelect }) {
  return (
    <div className="versions">
      {CARDS.map((card) => (
        <div
          key={card.id}
          data-version={card.id}
          className={`version${version === card.id ? " selected" : ""}`}
          onClick={() => onSelect(card.id)}
        >
          <div className="version-bg" style={{ backgroundImage: `url(${card.bg})` }}></div>
          <div className="version-line"></div>

          <div className="version-content">
            <div className="version-tag">{card.tag}</div>
            <h3>{card.title}</h3>
            <p>{card.desc}</p>
          </div>

          <div className="check">✓</div>
        </div>
      ))}
    </div>
  );
}
