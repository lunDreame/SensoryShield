import { Metric } from "../../shared/ui/Metric";
import { Panel } from "../../shared/ui/Panel";
import type { SensorStatus } from "../../shared/types/domain";

interface EnvironmentSummaryProps {
  sensor: SensorStatus;
}

export function EnvironmentSummary({ sensor }: EnvironmentSummaryProps) {
  return (
    <Panel title="환경 상태" subtitle="지금 공간에서 측정한 값" className="environment-panel">
      <div className="metric-grid">
        <Metric label="주변 밝기" value={sensor.illuminanceValid ? `${sensor.lux.toFixed(0)} lx` : "연결 대기"} detail={sensor.illuminanceValid ? "현재 측정값" : "기기 연결 후 표시돼요"} />
        <Metric label="사람 움직임" value={sensor.pirValid ? (sensor.occupied ? "감지했어요" : "감지되지 않아요") : "연결 대기"} detail={sensor.pirValid ? "현재 상태" : "기기 연결 후 표시돼요"} />
        <Metric label="주변 소리" value={sensor.micValid ? `${(sensor.soundEnergy * 100).toFixed(1)}%` : "측정 불가"} detail={sensor.micValid ? "마이크 상대 입력 크기 · dB 측정값이 아니에요" : "마이크 상태를 확인해 주세요"} />
        <Metric label="공간의 자극 정도" value={sensor.illuminanceValid || sensor.micValid ? sensor.sensoryScore.toFixed(1) : "--"} detail="빛과 소리 변화" />
      </div>
    </Panel>
  );
}
