import { useEffect, useState } from "react";
import { factoryReset, getDiagnostics } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import { SystemStatus } from "../../widgets/system-status/SystemStatus";
import type { DiagnosticsResponse, FanDiagnostics, StatusResponse } from "../../shared/types/domain";

interface DiagnosticsPageProps { status: StatusResponse; connectionError: boolean; }

function fanState(fan: FanDiagnostics) {
  if (!fan.outputValid) return { label: "PWM 적용 오류", badge: "badge badge-red" };
  if (fan.boosting) return { label: "기동 부스트 중", badge: "badge badge-blue" };
  if (fan.stabilizationWaiting) return { label: "안정화 대기", badge: "badge badge-yellow" };
  return { label: "PWM 적용 정상", badge: "badge badge-green" };
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
      <Panel title="센서 상태" subtitle="센서별 입력 확인"><div className="status-list">
        <span>조도 센서</span><b className={status.sensor.illuminanceValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.illuminanceValid ? "정상" : "확인 필요"}</b>
        <span>마이크</span><b className={status.sensor.micValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.micValid ? "정상" : "확인 필요"}</b>
        <span>재실 센서</span><b className={status.sensor.pirValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.pirValid ? "정상" : "확인 필요"}</b>
      </div></Panel>
      <SystemStatus system={status.system} />
      {mic ? <Panel title="마이크 수신 상태" subtitle="새 측정값이 계속 들어오는지 확인합니다">
        {unavailable ? <p className="inline-error">기기 연결 실패</p> : <div className="status-list">
          <span>수신 상태</span><b className={mic.fresh ? "badge badge-green" : "badge badge-yellow"}>{mic.fresh ? "정상 수신" : mic.recovering ? "수신 복구 중" : "새 측정값 대기"}</b>
          <span>수신 표본 수</span><b>{mic.sampleCount}</b>
          <span>마지막 수신 후</span><b>{mic.sampleCount ? `${(mic.sampleAgeMs / 1000).toFixed(1)}초` : "아직 없음"}</b>
          <span>복구 시도</span><b>{mic.restartCount}회</b>
          <span>현재 오류</span><b>{mic.lastError || "없음"}</b>
        </div>}
      </Panel> : null}
      {diagnostics?.demo && !unavailable ? <Panel title="환경 비교 기준" subtitle={diagnostics.demo.enabled ? "시연 빌드" : "실사용 빌드"}>
        <p>{diagnostics.demo.baselineFrozen ? "리허설 실측 표본을 비교 기준으로 고정했어요. 실시간 감지와 제어는 계속해요." : "실시간 측정값으로 평소 환경을 자동 학습해요."}</p>
      </Panel> : null}
      <Panel title="팬 PWM 진단" subtitle="전기적 구동 명령 상태이며 실제 RPM·회전·풍량은 아닙니다">
        {unavailable ? <p className="inline-error" role="alert">기기 연결 실패</p> : !fan ? <p className="profile-setting-note" role="status">팬 PWM 진단 정보 없음</p> :
          <div className="status-list">
            <span>목표 PWM</span><b>{fan.targetPercent}%</b><span>마지막 성공 PWM</span><b>{fan.appliedDutyPercent}%</b>
            <span>팬 상태</span><b className={state?.badge}>{state?.label}</b>
            <span>부스트</span><b className={fan.boosting ? "badge badge-blue" : "badge"}>{fan.boosting ? "진행 중" : "아님"}</b>
            <span>적용 상태</span><b className={fan.outputValid ? "badge badge-green" : "badge badge-red"}>{fan.outputValid ? "유효" : "무효"}</b>
            <span>마지막 오류</span><b>{fan.lastError === 0 ? "없음" : fan.lastError}</b>
            <span>성공 출력 변경 경과</span><b>{fan.changedAgoMs < 1000 ? `${fan.changedAgoMs} ms` : `${(fan.changedAgoMs / 1000).toFixed(1)} 초`}</b>
            <span>팬 출력 안정화</span><b className={fan.stabilizationWaiting ? "badge badge-yellow" : "badge badge-green"}>{fan.stabilizationWaiting ? "대기 중" : "완료"}</b>
            <span>전체 제어 입력</span><b className={fan.controlInputsSettled ? "badge badge-green" : "badge badge-yellow"}>{fan.controlInputsSettled ? "안정" : "대기 중"}</b>
          </div>}
        {fan && !fan.outputValid ? <p className="profile-setting-note">마지막 성공 duty는 기록이며, 오류 후 현재 물리 출력을 보장하지 않아요.</p> : null}
      </Panel>
      <Panel title="설정 초기화" subtitle="저장된 설정을 지우고 처음 상태로 되돌립니다."><div className="control-row">
        <span className="save-state" role="status">{resetState || "초기화하면 기기를 다시 설정해야 합니다."}</span>
        <button className="button button-danger" type="button" disabled={!status.system.ready} onClick={() => void resetDevice()}>초기화</button>
      </div></Panel>
    </main>
  );
}
