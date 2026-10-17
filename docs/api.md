# REST API

보드의 HTTP 80 포트에서 JSON을 주고받습니다. 요청 본문은 최대 511바이트입니다. 변경 요청의 성공 응답은 기본적으로 `{"ok":true}`이며, 실제 적용 상태는 상태 API로 확인합니다.

## 엔드포인트

| 경로 | 메서드 | 용도 / 본문 |
| --- | --- | --- |
| `/api/status` | GET | `sensor`, `outputs`, `system` 상태 |
| `/api/diagnostics` | GET | 설정·학습·마이크·팬·연결 진단 |
| `/api/profile` | GET, POST | 보호자 설정 조회·저장 |
| `/api/light` | POST | `power`, `brightness`, `cct`; 선택: `rgbMode`, `red`, `green`, `blue` |
| `/api/fan` | POST | `power`, `speed` |
| `/api/mode` | POST | `mode`: `AUTO`·`MANUAL`·`OVERRIDE`, `durationMinutes` |
| `/api/comfort` | POST | `action`: `BRIGHT`·`DARK`·`LOUD`·`QUIET` |
| `/api/environment-measurement` | GET, POST | 진단용 수집 상태 조회; POST `action`: `start`·`cancel` |
| `/api/factory-reset` | POST | `confirm: true`로 저장 설정 초기화 |

모든 API는 OPTIONS를 지원합니다. 잘못된 요청은 400, 내부 메모리·입출력 오류는 500, 본문 크기 초과는 413을 반환합니다. 아이 요청으로 바꿀 수 있는 출력이 없으면 `{"ok":true,"changed":false}`를 반환합니다.

조명 밝기와 팬 세기는 %, `cct`는 mired, RGB 채널은 0~255입니다. 제어·보호자 범위와 모드 동작은 [자동 제어와 설정](control.md)을 따릅니다.

## 프로필

POST에는 아래 필수 항목을 모두 보냅니다. 선택 항목을 생략하면 저장값을 유지합니다.

| 필드 | 범위 / 의미 |
| --- | --- |
| `lightWeight`, `soundWeight` | 각각 0~1로 저장, 웹에서는 합이 0 초과 |
| `minBrightness`, `maxBrightness` | 0~100, 하한 ≤ 상한 |
| `minCCTMireds`, `maxCCTMireds` | 1~1,000 mired, 하한 ≤ 상한 (웹 설정은 250~454) |
| `fanMaxPercent` | 0~100 |
| `occupancyTimeoutMs` | 1,000~3,600,000 ms |
| `profileConfigured` | 초기 설정 완료 여부 |
| `preferredBrightness` (선택) | 밝기 허용 범위 내 평소 밝기 |
| `preferredFanPercent` (선택) | 0~팬 상한, 평소 팬 세기 |
| `normalCCTMireds` (선택) | 허용 범위 내 평소 색온도 |
| `preferredCCTMireds` (선택) | 허용 범위 내 완화 색온도 |
| `reliefFanPercent` (선택) | 0 또는 18~100 |
| `reliefHoldMs` (선택) | 0~3,600,000 ms |
| `recoverConfirmMs` (선택) | 250~60,000 ms |
| `occupancyAutomationEnabled` (선택) | 움직임 기반 자동 종료 여부 |

`useMeasuredEnvironment: true`는 허용하지 않습니다. 환경 수집 API는 초기 설문에서 사용하지 않습니다.

## 상태와 진단

`/api/status`는 밝기, 움직임, 상대 소리 크기, 환경 점수, 출력과 모드를 반환합니다. `sensor.baselineReady`는 학습 입력·통계 창의 준비 여부이며 정확도 비율이 아닙니다. `system.matter`에는 등록 여부, fabric 수, Thread 연결 상태가 포함됩니다.

`/api/diagnostics`의 주요 항목:

- `microphone`: 표본 수, 마지막 수신 경과, 재시작 횟수, 오류, `fresh`·`recovering` 상태
- `fan`: 목표 PWM, 마지막 적용 duty, 기동 중 여부, 출력 유효성, 오류, 출력 변경 경과, 안정화 대기, 전체 입력 안정 상태
- `demo`: 시연 설정·고정 비교 기준 적용 여부

마이크는 500 ms 읽기 제한과 재시작 복구를 사용하고, 1초 이상 된 표본은 무효로 처리합니다. 팬의 마지막 성공 duty는 오류 발생 후 현재 출력을 보장하지 않습니다.
