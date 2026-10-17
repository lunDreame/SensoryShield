import { useEffect, useState } from "react";
import { factoryReset, getDiagnostics } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import { SystemStatus } from "../../widgets/system-status/SystemStatus";
import type { DiagnosticsResponse, FanDiagnostics, StatusResponse } from "../../shared/types/domain";

interface DiagnosticsPageProps { status: StatusResponse; connectionError: boolean; }

function fanState(fan: FanDiagnostics) {
  if (!fan.outputValid) return { label: "팬 조절 확인 필요", badge: "badge badge-red" };
  if (fan.boosting) return { label: "팬을 켜는 중", badge: "badge badge-blue" };
  if (fan.stabilizationWaiting) return { label: "조절 후 확인 중", badge: "badge badge-yellow" };
  return { label: "설정 적용됨", badge: "badge badge-green" };
}

export function DiagnosticsPage({ status, connectionError }: DiagnosticsPageProps) {
  const [resetState, setResetState] = useState("");
  const [diagnostics, setDiagnostics] = useState<DiagnosticsResponse | null>(null);
  const [diagnosticsError, setDiagnosticsError] = useState(false);

  useEffect(() => {
    let active = true;
    let timer: number | undefined;
    async function poll() {
      try {
        const next = await getDiagnostics();
        if (active) { setDiagnostics(next); setDiagnosticsError(false); }
      } catch { if (active) setDiagnosticsError(true); }
      finally { if (active) timer = window.setTimeout(poll, 1000); }
    }
    void poll();
    return () => { active = false; window.clearTimeout(timer); };
  }, []);

  async function resetDevice() {
    if (!window.confirm("저장된 설정을 모두 지울까요? 이 작업은 되돌릴 수 없습니다.")) return;
    setResetState("초기화 중");
    try { await factoryReset(); setResetState("초기화 요청 완료"); }
    catch { setResetState("기기 연결을 확인해 주세요"); }
  }

  const fan = diagnostics?.fan;
  const mic = diagnostics?.microphone;
  const state = fan ? fanState(fan) : null;
  const unavailable = connectionError || diagnosticsError;
  return (
    <main className="page-grid">
      <section className="overview"><div><h1>기기 상태</h1><p>센서와 저장된 설정 상태를 확인할 수 있어요.</p></div></section>
      <Panel title="센서 상태" subtitle="밝기, 소리, 움직임 감지 상태"><div className="status-list">
        <span>밝기 감지</span><b className={status.sensor.illuminanceValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.illuminanceValid ? "정상" : "확인 필요"}</b>
        <span>마이크</span><b className={status.sensor.micValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.micValid ? "정상" : "확인 필요"}</b>
        <span>움직임 감지</span><b className={status.sensor.pirValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.pirValid ? "정상" : "확인 필요"}</b>
      </div></Panel>
      <SystemStatus system={status.system} />
      {mic ? <Panel title="소리 측정 상태" subtitle="주변 소리를 측정하고 있어요">
        {unavailable ? <p className="inline-error">기기 연결을 확인해 주세요</p> : <div className="status-list">
          <span>수신 상태</span><b className={mic.fresh ? "badge badge-green" : "badge badge-yellow"}>{mic.fresh ? "정상 수신" : mic.recovering ? "수신 복구 중" : "새 측정값 대기"}</b>
          <span>마지막 수신 후</span><b>{mic.sampleCount ? `${(mic.sampleAgeMs / 1000).toFixed(1)}초` : "아직 없음"}</b>
          <span>측정 상태</span><b>{mic.lastError ? "확인 필요" : "오류 없음"}</b>
        </div>}
      </Panel> : null}
      {diagnostics?.demo && !unavailable ? <Panel title="환경 비교 기준" subtitle="빛과 소리 변화를 살펴보는 기준">
        <p>{diagnostics.demo.baselineFrozen ? "저장된 환경을 기준으로 빛과 소리 변화를 살펴봐요." : "실시간 측정값으로 평소 환경을 자동 학습해요."}</p>
      </Panel> : null}
      <Panel title="팬 조절 상태" subtitle="설정 적용 상태예요. 실제 바람 세기와 다를 수 있어요.">
        {unavailable ? <p className="inline-error" role="alert">기기 연결을 확인해 주세요</p> : !fan ? <p className="profile-setting-note" role="status">팬 상태를 아직 확인할 수 없어요.</p> :
          <div className="status-list">
            <span>설정한 세기</span><b>{fan.targetPercent}%</b><span>마지막 적용 세기</span><b>{fan.appliedDutyPercent}%</b>
            <span>팬 상태</span><b className={state?.badge}>{state?.label}</b>
            <span>자동 조절 준비</span><b className={fan.controlInputsSettled ? "badge badge-green" : "badge badge-yellow"}>{fan.controlInputsSettled ? "안정" : "대기 중"}</b>
          </div>}
        {fan && !fan.outputValid ? <p className="profile-setting-note">팬 상태를 확인할 수 없어요. 마지막 적용 세기는 현재 상태와 다를 수 있어요.</p> : null}
      </Panel>
      <Panel title="설정 초기화" subtitle="저장된 설정을 지우고 처음 상태로 되돌립니다."><div className="control-row">
        <span className="save-state" role="status">{resetState || "초기화하면 기기를 다시 설정해야 합니다."}</span>
        <button className="button button-danger" type="button" disabled={!status.system.ready} onClick={() => void resetDevice()}>초기화</button>
      </div></Panel>
    </main>
  );
}
