import { useState } from "react";
import { factoryReset } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import { SystemStatus } from "../../widgets/system-status/SystemStatus";
import type { StatusResponse } from "../../shared/types/domain";

interface DiagnosticsPageProps {
  status: StatusResponse;
}

export function DiagnosticsPage({ status }: DiagnosticsPageProps) {
  const [resetState, setResetState] = useState("");

  async function resetDevice() {
    if (!window.confirm("저장된 설정을 모두 지울까요? 이 작업은 되돌릴 수 없습니다.")) {
      return;
    }
    setResetState("초기화 중");
    try {
      await factoryReset();
      setResetState("초기화 요청 완료");
    } catch {
      setResetState("기기 연결을 확인해 주세요");
    }
  }

  return (
    <main className="page-grid">
      <section className="overview">
        <div>
          <h1>기기 상태</h1>
          <p>센서와 저장된 설정 상태를 확인할 수 있어요.</p>
        </div>
      </section>
      <Panel title="센서 상태" subtitle="센서별 입력 확인">
        <div className="status-list">
          <span>조도 센서</span><b className={status.sensor.illuminanceValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.illuminanceValid ? "정상" : "확인 필요"}</b>
          <span>마이크</span><b className={status.sensor.micValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.micValid ? "정상" : "확인 필요"}</b>
          <span>재실 센서</span><b className={status.sensor.pirValid ? "badge badge-green" : "badge badge-red"}>{status.sensor.pirValid ? "정상" : "확인 필요"}</b>
        </div>
      </Panel>
      <SystemStatus system={status.system} />
      <Panel title="설정 초기화" subtitle="저장된 설정을 지우고 처음 상태로 되돌립니다">
        <div className="control-row">
          <span className="save-state" role="status">{resetState || "초기화 후 기기를 다시 설정해야 합니다."}</span>
          <button className="button button-danger" type="button" disabled={!status.system.ready} onClick={() => void resetDevice()}>초기화</button>
        </div>
      </Panel>
    </main>
  );
}
