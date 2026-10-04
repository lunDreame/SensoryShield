import { useEffect, useState } from "react";
import { DashboardPage } from "../pages/dashboard/DashboardPage";
import { DiagnosticsPage } from "../pages/diagnostics/DiagnosticsPage";
import { ProfileOnboarding } from "../pages/onboarding/ProfileOnboarding";
import { SettingsPage } from "../pages/settings/SettingsPage";
import { getProfile } from "../shared/api/client";
import { useStatus } from "../shared/hooks/useStatus";
import type { AppConfig } from "../shared/types/domain";

type Route = "dashboard" | "settings" | "diagnostics";

export function App() {
  const [route, setRoute] = useState<Route>("dashboard");
  const [profile, setProfile] = useState<AppConfig | null>(null);
  const [profileChecked, setProfileChecked] = useState(false);
  const { status, error, updatedAt } = useStatus();

  useEffect(() => {
    let active = true;
    void getProfile()
      .then((stored) => { if (active) setProfile(stored); })
      .catch(() => { /* The regular connection state explains an unavailable device. */ })
      .finally(() => { if (active) setProfileChecked(true); });
    return () => { active = false; };
  }, []);

  if (profileChecked && profile && !profile.profileConfigured) {
    return <ProfileOnboarding initialConfig={profile} onComplete={setProfile} />;
  }

  return (
    <div className="app-shell">
      <nav className="topbar">
        <div className="nav-tabs" aria-label="주요 화면">
          <button className={route === "dashboard" ? "active" : ""} type="button" onClick={() => setRoute("dashboard")}>홈</button>
          <button className={route === "settings" ? "active" : ""} type="button" onClick={() => setRoute("settings")}>설정</button>
          <button className={route === "diagnostics" ? "active" : ""} type="button" onClick={() => setRoute("diagnostics")}>기기 상태</button>
        </div>
        <span className={`connection-status${status.system.ready ? " is-ready" : ""}`} role="status">
          <i className="connection-dot" />{status.system.ready ? "기기 연결됨" : error ? "기기 연결 실패" : "기기 연결 전"}
        </span>
      </nav>
      {route === "dashboard" ? <DashboardPage status={status} updatedAt={updatedAt} connectionError={error !== null} /> : null}
      {route === "settings" ? <SettingsPage /> : null}
      {route === "diagnostics" ? <DiagnosticsPage status={status} /> : null}
    </div>
  );
}
