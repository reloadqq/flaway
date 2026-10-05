import { createRoot } from "react-dom/client";

import "./styles/base.css";
import "./styles/shell.css";
import "./styles/home.css";
import "./styles/pages.css";
import "./styles/overlays.css";
import "./styles/extras.css";

import App from "./App.jsx";

createRoot(document.getElementById("root")).render(<App />);
