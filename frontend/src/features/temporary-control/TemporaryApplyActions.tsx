import { useState } from "react";
import { formatDurationMinutes } from "../../shared/lib/duration";

const durationOptions = [10, 30, 60, 120];

interface TemporaryApplyActionsProps {
  disabled: boolean;
  pending: boolean;
  onApply: (mode: "MANUAL" | "OVERRIDE", durationMinutes?: number) => void;
}

export function TemporaryApplyActions({ disabled, pending, onApply }: TemporaryApplyActionsProps) {
  const [minutes, setMinutes] = useState(10);

  return (
    <div className="temporary-apply">
      <div className="temporary-duration">
        <span>자동으로 돌아갈 시간</span>
        <div className="duration-presets" role="group" aria-label="자동 복귀 시간">
          {durationOptions.map((option) => <button className={minutes === option ? "active" : ""} type="button"
            aria-pressed={minutes === option} onClick={() => setMinutes(option)} key={option}>{formatDurationMinutes(option)}</button>)}
        </div>
      </div>
      <div className="control-apply-actions">
        <button className="button button-weak" type="button" disabled={disabled || pending}
          onClick={() => onApply("OVERRIDE", minutes)}>{formatDurationMinutes(minutes)} 동안 사용</button>
        <button className="button button-primary" type="button" disabled={disabled || pending}
          onClick={() => onApply("MANUAL")}>{pending ? "적용 중" : "계속 유지"}</button>
      </div>
    </div>
  );
}
