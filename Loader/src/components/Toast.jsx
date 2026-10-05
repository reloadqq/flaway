export default function Toast({ toast }) {
  return (
    <div className={`toast${toast.visible ? " show" : ""}`} id="toast">
      <div className="toast-title">{toast.title}</div>
      <div className="toast-text" id="toastText">{toast.text}</div>
    </div>
  );
}
