import "./sound-metric.css";
import { Metric } from "../../shared/ui/Metric";
import { Panel } from "../../shared/ui/Panel";
import { describeLightLevel } from "../../shared/lib/environment-labels";
import type { SensorStatus } from "../../shared/types/domain";

interface EnvironmentSummaryProps {
  sensor: SensorStatus;
}

export function EnvironmentSummary({ sensor }: EnvironmentSummaryProps) {
  const soundValid = sensor.micValid && Number.isFinite(sensor.soundEnergy) && sensor.soundEnergy >= 0 && sensor.soundEnergy <= 1;
  const soundPercent = soundValid ? sensor.soundEnergy * 100 : 0;

  return (
    <Panel title="환경 상태" subtitle="지금 공간에서 측정한 값" className="environment-panel">
      <div className="metric-grid">
        <Metric label="주변 밝기" value={sensor.illuminanceValid ? `${sensor.lux.toFixed(0)} lx` : "연결 대기"} detail={sensor.illuminanceValid ? describeLightLevel(sensor.lux) : "기기 연결 후 표시돼요"} />
        <Metric label="사람 움직임" value={sensor.pirValid ? (sensor.occupied ? "감지했어요" : "감지되지 않아요") : "연결 대기"} detail={sensor.pirValid ? "현재 상태" : "기기 연결 후 표시돼요"} />
        <div className={`metric sound-metric${soundValid ? "" : " sound-metric-unavailable"}`}>
          <span>주변 소리</span>
          <strong>{soundValid ? `${soundPercent.toFixed(1)}%` : "측정 불가"}</strong>
          {soundValid ? <div className="sound-input-track" aria-hidden="true"><div style={{ width: `${soundPercent}%` }} /></div> : null}
          <small>{soundValid ? "마이크 상대 입력 · dB 아님" : "마이크 연결 상태를 확인하세요"}</small>
        </div>
        <Metric label="공간의 자극 정도" value={sensor.illuminanceValid || sensor.micValid ? sensor.sensoryScore.toFixed(1) : "--"} detail="빛과 소리 변화" />
      </div>
    </Panel>
  );
}
