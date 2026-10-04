import { useState } from "react";
import { formatDurationMinutes } from "../../shared/lib/duration";

interface TemporaryApplyActionsProps {
  disabled: boolean;
  pending: boolean;
  onApply: (mode: "MANUAL" | "OVERRIDE", durationMinutes?: number) => void;
}

export function TemporaryApplyActions({ disabled, pending, onApply }: TemporaryApplyActionsProps) {
  const [rawHours, setRawHours] = useState("0");
  const [rawMinutes, setRawMinutes] = useState("15");
  const hours = Number(rawHours);
  const minutePart = Number(rawMinutes);
  const totalMinutes = hours * 60 + minutePart;
  const valid = Number.isInteger(hours) && Number.isInteger(minutePart)
    && hours >= 0 && hours <= 24 && minutePart >= 0 && minutePart <= 59
    && totalMinutes >= 1 && totalMinutes <= 1440;

  return (
    <div className="temporary-apply">
      <div className="temporary-duration">
        <span>자동으로 돌아갈 시간</span>
        <div className="duration-fields" role="group" aria-label="자동 복귀 시간">
          <label><input aria-label="자동 복귀 시간" type="number" min={0} max={24} step={1}
            value={rawHours} onChange={(event) => setRawHours(event.target.value)} /><span>시간</span></label>
          <label><input aria-label="자동 복귀 분" type="number" min={0} max={59} step={1}
            value={rawMinutes} onChange={(event) => setRawMinutes(event.target.value)} /><span>분</span></label>
        </div>
      </div>
      {!valid ? <p className="inline-error">1분 이상 24시간 이내로 입력해 주세요.</p> : null}
      <div className="control-apply-actions">
        <button className="button button-weak" type="button" disabled={disabled || pending || !valid}
          onClick={() => onApply("OVERRIDE", totalMinutes)}>{valid ? `${formatDurationMinutes(totalMinutes)} 동안 사용` : "시간 확인"}</button>
        <button className="button button-primary" type="button" disabled={disabled || pending}
          onClick={() => onApply("MANUAL")}>{pending ? "적용 중" : "계속 유지"}</button>
      </div>
    </div>
  );
}
