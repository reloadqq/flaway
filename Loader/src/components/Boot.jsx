export default function Boot({ hidden }) {
  return (
    <div className={`loader-screen${hidden ? " hidden" : ""}`} id="boot">
      <div className="loader-logo">F</div>
      <div className="loader-name">flaway dlc</div>
      <div className="loader-text">loading interface...</div>
      <div className="loading-bar">
        <span></span>
      </div>
    </div>
  );
}
