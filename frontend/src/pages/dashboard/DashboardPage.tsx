import { ModeSwitch } from "../../features/change-mode/ModeSwitch";
import { CCTLightControl } from "../../widgets/CCTLight-control/CCTLightControl";
import { EnvironmentSummary } from "../../widgets/environment-summary/EnvironmentSummary";
import { FanControl } from "../../widgets/fan-control/FanControl";
import type { StatusResponse } from "../../shared/types/domain";

interface DashboardPageProps {
  status: StatusResponse;
}

export function DashboardPage({ status }: DashboardPageProps) {
  return (
    <main className="page-grid">
      <section className="overview">
        <div className="overview-copy">
          <h1>우리 집 환경</h1>
          <p>조명과 바람을 조절하고 공간 상태를 살펴보세요.</p>
        </div>
        <time>{new Intl.DateTimeFormat("ko-KR", { hour: "2-digit", minute: "2-digit" }).format(new Date())} 기준</time>
      </section>
      {!status.system.ready ? <div className="notice connection-notice"><i className="notice-dot" />기기와 연결되면 현재 환경을 보여드릴게요.</div> : null}
      <section className="panel score-panel" aria-label="감각 점수">
        <div className="panel-header">
          <div><h2>공간의 자극 정도</h2><p className="panel-subtitle">빛과 소리 변화 기준</p></div>
          <span className="badge" style={{ background: "#ffffff26", color: "#fff" }}>현재</span>
        </div>
        <div>
          <div className="score-number"><strong>{status.system.ready ? status.sensor.sensoryScore.toFixed(1) : "--"}</strong><span className="score-unit">{status.system.ready ? "/ 8" : ""}</span></div>
          <p className="score-description">{!status.system.ready ? "기기 연결 후 확인할 수 있어요." : status.sensor.sensoryScore < 2.8 ? "현재 공간이 차분해요." : status.sensor.sensoryScore < 5.6 ? "공간에 변화가 있어요." : "빛이나 소리가 평소보다 강해요."}</p>
          <div className="score-track"><span style={{ width: `${status.system.ready ? Math.min(100, Math.max(0, status.sensor.sensoryScore / 8 * 100)) : 0}%` }} /></div>
        </div>
      </section>
      <EnvironmentSummary sensor={status.sensor} />
      <CCTLightControl outputs={status.outputs} available={status.system.ready} />
      <FanControl outputs={status.outputs} available={status.system.ready} />
      <section className="panel mode-panel">
        <div className="panel-header"><div><h2>작동 방식</h2><p className="panel-subtitle">자동으로 맡기거나 직접 조절할 수 있어요.</p></div></div>
        <ModeSwitch mode={status.system.mode} available={status.system.ready} />
      </section>
    </main>
  );
}
