import { useState } from "react";

interface TemporaryApplyActionsProps {
  disabled: boolean;
  pending: boolean;
  onApply: (mode: "MANUAL" | "OVERRIDE", durationMinutes?: number) => void;
}

export function TemporaryApplyActions({ disabled, pending, onApply }: TemporaryApplyActionsProps) {
  const [rawMinutes, setRawMinutes] = useState("15");
  const minutes = Number(rawMinutes);
  const valid = Number.isInteger(minutes) && minutes >= 1 && minutes <= 1440;

  return (
    <div className="temporary-apply">
      <label className="temporary-duration">
        <span>자동으로 돌아갈 시간</span>
        <span className="field-with-unit">
          <input aria-label="자동으로 돌아갈 시간" type="number" min={1} max={1440} step={1}
            value={rawMinutes} onChange={(event) => setRawMinutes(event.target.value)} />
          <em>분</em>
        </span>
      </label>
      {!valid && rawMinutes !== "" ? <p className="inline-error">1분에서 1,440분 사이로 입력해 주세요.</p> : null}
      <div className="control-apply-actions">
        <button className="button button-weak" type="button" disabled={disabled || pending || !valid}
          onClick={() => onApply("OVERRIDE", minutes)}>{valid ? `${minutes}분간 사용` : "시간 지정"}</button>
        <button className="button button-primary" type="button" disabled={disabled || pending}
          onClick={() => onApply("MANUAL")}>{pending ? "적용 중" : "계속 유지"}</button>
      </div>
    </div>
  );
}
