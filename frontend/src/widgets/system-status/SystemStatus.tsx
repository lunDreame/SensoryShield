import { Panel } from "../../shared/ui/Panel";
import type { SystemHealth } from "../../shared/types/domain";

interface SystemStatusProps {
  system: SystemHealth;
}

export function SystemStatus({ system }: SystemStatusProps) {
  return (
    <Panel title="기기와 연결" subtitle="현재 사용할 수 있는 기능">
      <div className="status-list">
        <span>기기 상태</span><b className={system.ready ? "badge badge-green" : "badge"}>{system.ready ? "연결됨" : "연결 전"}</b>
        <span>스마트홈 연동</span><b className="badge">현재 사용할 수 없어요</b>
        <span>설정</span><b className={system.storage.appConfig ? "badge badge-blue" : "badge"}>{system.storage.appConfig ? "저장되어 있어요" : "기본값 사용 중"}</b>
      </div>
    </Panel>
  );
}
