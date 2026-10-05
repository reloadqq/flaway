const NAV = [
  { page: "home", icon: "⌂", label: "Home" },
  { page: "subscription", icon: "◈", label: "Subscription" },
  { page: "settings", icon: "⚙", label: "Settings", iconClass: "settings-icon" }
];

export default function Sidebar({ page, setPage, onDownload }) {
  return (
    <aside className="sidebar">
      <div className="logo">
        <div className="logo-icon">F</div>
        <div className="logo-text">
          flaway <span>dlc</span>
        </div>
      </div>

      <nav className="nav">
        {NAV.map((item) => (
          <button
            key={item.page}
            type="button"
            data-page={item.page}
            className={page === item.page ? "active" : ""}
            onClick={() => setPage(item.page)}
          >
            <div
              className={`nav-icon${item.iconClass ? " " + item.iconClass : ""}`}
            >
              {item.icon}
            </div>
            <span>{item.label}</span>
          </button>
        ))}
      </nav>

      <button type="button" id="download" className="download" onClick={onDownload}>
        ↓ &nbsp; Download
      </button>

      <div className="profile">
        <img className="avatar" src="/profile.jpg" alt="" />
        <div>
          <div className="profile-name">flaway dlc</div>
          <div className="profile-status">
            <i className="online"></i>
            premium
          </div>
        </div>
      </div>
    </aside>
  );
}
