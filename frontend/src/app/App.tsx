import { useEffect, useState } from "react";
import { DashboardPage } from "../pages/dashboard/DashboardPage";
import { DiagnosticsPage } from "../pages/diagnostics/DiagnosticsPage";
import { ProfileOnboarding } from "../pages/onboarding/ProfileOnboarding";
import { SettingsPage } from "../pages/settings/SettingsPage";
import { ChildModePage } from "../pages/child/ChildModePage";
import { getProfile } from "../shared/api/client";
import { useStatus } from "../shared/hooks/useStatus";
import type { AppConfig } from "../shared/types/domain";

type Route = "dashboard" | "settings" | "diagnostics";
type ViewMode = "child" | "guardian";

export function App() {
  const [route, setRoute] = useState<Route>("dashboard");
  const [viewMode, setViewMode] = useState<ViewMode>(() => localStorage.getItem("sensoryshield-view") === "child" ? "child" : "guardian");
  const [profile, setProfile] = useState<AppConfig | null>(null);
  const [setupComplete, setSetupComplete] = useState(false);
  const [profileError, setProfileError] = useState(false);
  const [profileAttempt, setProfileAttempt] = useState(0);
  const { status, error, updatedAt } = useStatus();

  useEffect(() => {
    let active = true;
    setProfileError(false);
    void getProfile()
      .then((stored) => { if (active) setProfile(stored); })
      .catch(() => { if (active) setProfileError(true); });
    return () => { active = false; };
  }, [profileAttempt]);

  // A new page visit starts with the survey even if another computer previously
  // configured this board. Loading/failure must never expose the dashboard.
  if (!profile) {
    return <main className="onboarding-shell"><section className="onboarding-card" aria-live="polite">
      <div className="onboarding-brand"><img src="/brand/sensoryshield-logo.png" alt="SensoryShield" /></div>
      <h1>{profileError ? "기기 설정을 불러오지 못했어요" : "처음 설정을 준비하고 있어요"}</h1>
      <p className="onboarding-description">{profileError ? "기기 연결을 확인하고 다시 시도해 주세요." : "저장된 설정을 확인한 뒤 민감도와 완화 빛을 선택해 주세요."}</p>
      {profileError ? <button className="button button-primary" type="button" onClick={() => setProfileAttempt(value => value + 1)}>다시 시도</button> : null}
    </section></main>;
  }
  if (!setupComplete || !profile.profileConfigured) {
    return <ProfileOnboarding initialConfig={profile} onComplete={next => {
      setProfile(next); setSetupComplete(true); setViewMode("guardian"); setRoute("dashboard");
    }} />;
  }

  function changeViewMode(next: ViewMode) {
    setViewMode(next);
    localStorage.setItem("sensoryshield-view", next);
    if (next === "guardian") setRoute("dashboard");
  }

  return (
    <div className="app-shell">
      <nav className="topbar">
        <div className="topbar-main">
          <button className="brand-home" type="button" aria-label="SensoryShield 홈" onClick={() => setRoute("dashboard")}>
            <img className="brand-full" src="/brand/sensoryshield-logo.png" alt="" />
            <img className="brand-mark" src="/brand/sensoryshield-mark.png" alt="" />
          </button>
          <div className={`nav-tabs${viewMode === "child" ? " is-hidden" : ""}`} aria-label="주요 화면">
            <button className={route === "dashboard" ? "active" : ""} type="button" onClick={() => setRoute("dashboard")}>홈</button>
            <button className={route === "settings" ? "active" : ""} type="button" onClick={() => setRoute("settings")}>설정</button>
            <button className={route === "diagnostics" ? "active" : ""} type="button" onClick={() => setRoute("diagnostics")}>기기 상태</button>
          </div>
        </div>
        <div className="topbar-actions">
          <div className="view-switch" aria-label="사용 화면">
            <button className={viewMode === "child" ? "active" : ""} type="button" aria-pressed={viewMode === "child"} onClick={() => changeViewMode("child")}>
              <img src="/illustrations/child-avatar.png" alt="" /><span><b>아이</b><small>쉬운 조작</small></span>
            </button>
            <button className={viewMode === "guardian" ? "active" : ""} type="button" aria-pressed={viewMode === "guardian"} onClick={() => changeViewMode("guardian")}>
              <img src="/illustrations/adult-avatar.png" alt="" /><span><b>보호자</b><small>상세 관리</small></span>
            </button>
          </div>
          <span className={`connection-status${status.system.ready ? " is-ready" : ""}`} role="status">
            <i className="connection-dot" />{status.system.ready ? "기기 연결됨" : error ? "기기 연결 실패" : "기기 연결 전"}
          </span>
        </div>
      </nav>
      {viewMode === "child" ? <ChildModePage status={status} connectionError={error !== null} /> : (
        <>
          {route === "dashboard" ? <DashboardPage status={status} updatedAt={updatedAt} connectionError={error !== null} /> : null}
          {route === "settings" ? <SettingsPage onRestartOnboarding={setProfile} /> : null}
          {route === "diagnostics" ? <DiagnosticsPage status={status} connectionError={error !== null} /> : null}
        </>
      )}
    </div>
  );
}
