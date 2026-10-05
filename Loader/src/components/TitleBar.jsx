export default function TitleBar({ onMinimize, onClose }) {
  return (
    <div className="window-controls">
      <button
        type="button"
        className="window-btn"
        id="minimize"
        aria-label="Minimize"
        onClick={onMinimize}
      >
        —
      </button>

      <button
        type="button"
        className="window-btn close"
        id="close"
        aria-label="Close"
        onClick={onClose}
      >
        ×
      </button>
    </div>
  );
}
