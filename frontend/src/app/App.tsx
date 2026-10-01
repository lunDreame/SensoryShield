import { useState } from "react";
import { DashboardPage } from "../pages/dashboard/DashboardPage";
import { DiagnosticsPage } from "../pages/diagnostics/DiagnosticsPage";
import { SettingsPage } from "../pages/settings/SettingsPage";
import { useStatus } from "../shared/hooks/useStatus";

type Route = "dashboard" | "settings" | "diagnostics";

export function App() {
  const [route, setRoute] = useState<Route>("dashboard");
  const { status } = useStatus();

  return (
    <div className="app-shell">
      <nav className="topbar">
        <div className="nav-tabs" aria-label="주요 화면">
          <button className={route === "dashboard" ? "active" : ""} type="button" onClick={() => setRoute("dashboard")}>홈</button>
          <button className={route === "settings" ? "active" : ""} type="button" onClick={() => setRoute("settings")}>설정</button>
          <button className={route === "diagnostics" ? "active" : ""} type="button" onClick={() => setRoute("diagnostics")}>기기 상태</button>
        </div>
        <span className={`connection-status${status.system.ready ? " is-ready" : ""}`} role="status">
          <i className="connection-dot" />{status.system.ready ? "기기 연결됨" : "기기 연결 전"}
        </span>
      </nav>
      {route === "dashboard" ? <DashboardPage status={status} /> : null}
      {route === "settings" ? <SettingsPage /> : null}
      {route === "diagnostics" ? <DiagnosticsPage status={status} /> : null}
    </div>
  );
}
