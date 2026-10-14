export type ComfortAction = "BRIGHT" | "DARK" | "LOUD" | "QUIET";
interface ChildControlApi {
  requestComfort: (action: ComfortAction) => Promise<{ ok: boolean; changed?: boolean }>;
}
// One board command owns both actuators, arrival, hold and conditional AUTO return.
export async function applyChildComfort(action: ComfortAction, api: ChildControlApi): Promise<string> {
  let result;
  try {
    result = await api.requestComfort(action);
    if (!result.ok) throw new Error("rejected");
  } catch {
    throw new Error("지금은 기기에 알려주지 못했어요. 연결을 확인하고 다시 눌러주세요.");
  }
  if (result.changed === false) return "지금은 설정한 범위 안에서 더 바꿀 수 없어요. 보호자에게 알려주세요.";
  return {
    BRIGHT: "조명을 서서히 낮출게요.",
    DARK: "조명을 서서히 밝힐게요.",
    LOUD: "조명과 팬 세기를 서서히 낮출게요.",
    QUIET: "팬을 켜거나 바람 세기를 서서히 높일게요."
  }[action];
}
