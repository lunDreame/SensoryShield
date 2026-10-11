# SensoryShield

SensoryShield는 nRF54LM20 DK 기반의 로컬 감각 환경 제어 펌웨어입니다. Zephyr RTOS와 C++17을 사용하며, 로컬 센서 처리, WS2812B LED 링 및 PWM 팬 제어, 영구 앱 설정, Thread IPv6 네트워크, 로컬 HTTP REST API를 포함합니다.

## 펌웨어

펌웨어는 NCS sysbuild 기준으로 빌드합니다. MCUboot, Matter factory data, SensoryShield 애플리케이션이 함께 빌드되고, 전체 플래시 때 같은 순서로 보드에 기록됩니다.

처음 빌드하거나 빌드 설정이 바뀐 경우:

```sh
./scripts/build.sh --pristine
./scripts/flash.sh
```

평소 증분 빌드와 앱만 빠르게 다시 올리는 경우:

```sh
./scripts/build.sh
./scripts/flash.sh --app-only
```

`./scripts/flash.sh`는 `mcuboot`, `matter_factory_data`, `SensoryShield` 도메인을 모두 플래시합니다. `./scripts/flash.sh --app-only`는 이미 MCUboot와 factory data가 설치되어 있다는 전제에서 SensoryShield 앱 도메인만 업데이트합니다.

VS Code 작업도 같은 스크립트를 호출하도록 구성되어 있습니다.

## 프론트엔드

```sh
cd frontend
npm install
npm run dev
npm run build
```

펌웨어 빌드는 프론트엔드 production 빌드를 실행하고, 생성된 HTML, JavaScript, CSS를 압축해 펌웨어 이미지에 포함합니다. 보드에서는 Thread 네트워크에 붙은 뒤 로그에 출력되는 LAN-routable IPv6 주소로 웹 UI를 제공합니다.

개발 서버는 5173 포트에서 실행됩니다. 실제 보드로 `/api` 요청을 프록시하려면 `frontend/.env`에 `API_TARGET=http://[보드 IPv6 주소]`를 설정하거나 `API_TARGET='http://[보드 IPv6 주소]' npm run dev`처럼 대상 주소를 지정합니다. 보드가 없으면 프론트엔드는 오프라인 미리보기 상태로 동작하고 장치 제어는 비활성화됩니다.

REST API는 `/api/status`, `/api/light`, `/api/fan`, `/api/mode`, `/api/profile`, `/api/environment-baseline`, `/api/diagnostics`, `/api/factory-reset`을 제공합니다. `/api/light`는 `power`, `brightness`, `cct`, `rgbMode`, `red`, `green`, `blue`를 받을 수 있습니다. Matter ColorControl은 Level, ColorTemperature, Hue/Saturation 명령을 지원합니다. factory reset 엔드포인트는 확인 요청이 있는 경우에만 동작합니다.

## 현재 하드웨어 매핑

- BH1750 I2C: SDA `P1.12`, SCL `P1.13`
- MP34DT01 PDM: CLK `P1.14`, DATA `P1.11`; 16 kHz mono, 100 ms window
- HC-SR501 PIR: `P1.10`
- WS2812B 12 LED ring: `DI` -> `P3.00` 출력 가능 GPIO 패드, `5V` -> 5 V 전원, `GND` -> 공통 GND, `DO` -> 다음 LED 체인 연결용 또는 미사용
- Fan PWM: `P3.02`

WS2812B `DI`는 보드의 출력 가능한 GPIO 패드에 연결합니다. 현재 펌웨어는 `P3.00`을 WS2812B 데이터 파형 출력으로 사용합니다.

WS2812B 조명은 밝기 디밍, RGB 색상, 백색 색온도 제어를 지원합니다.

Thread REST 접속에는 보더 라우터가 LAN으로 광고한 OMR IPv6 주소를 사용합니다. 링크 로컬, mesh-local, RLOC 주소는 일반 LAN 클라이언트에서 직접 접근할 수 없습니다.

## 평소 환경 측정 기준

개인 설정의 마지막 단계에서 평소 환경을 측정하고 `설정 완료`를 누르면 비교 기준을 보드에 저장합니다. 설정 화면의 `평소 환경 다시 측정`으로 개인 민감도를 바꾸지 않고 새 기준을 저장할 수 있습니다.

- 측정 중에는 직전 조명·팬 출력을 유지합니다. 새 마이크 측정 시점이 포함된 유효 센서 응답 20개를 수집하며, 오래된 소리 입력과 중복 응답은 기준 수집에 사용하지 않습니다. 500 ms 간격 및 20개 표본은 MVP 취득 설정으로, 감각 불편의 임계값이 아닙니다.
- 밝기와 소리 에너지의 중앙값·MAD를 별도 버전의 `sensoryshield/environment_baseline` 설정 키에 저장합니다. 개인 설정의 형식과 버전은 유지합니다.
- 저장 성공 후 자극 점수와 팬 알고리즘에 같은 고정 비교 기준을 적용합니다. 각 알고리즘의 기존 변동 폭 최소값·민감도·제어 시간은 유지합니다. 기준은 자동으로 학습·변경되지 않으며 사용자가 재측정할 때 교체합니다.
- 재부팅 후 저장 기준을 복원하며, 기준이 없거나 손상된 경우 기존 자동 기준선 학습으로 동작합니다. 공장 초기화는 저장 기준도 삭제합니다. 측정한 환경이 편안하거나 적절하다는 판정을 의미하지 않습니다.
- `GET /api/environment-baseline`은 `configured`와 기준값을 반환합니다. `POST`는 정수 필드 `luxMedianMilli`, `luxMadMilli` (lux × 1000), `soundMedianMicro`, `soundMadMicro` (상대 에너지 × 1000000), `samples`를 받습니다. 광량 값은 0~65535 lux, 상대 소리 값은 0~1, 표본 수는 20~1000 범위로 검증합니다.
- 기준 저장과 개인 설정 저장은 별도 요청입니다. 개인 설정 저장만 실패하면 기준이 먼저 저장될 수 있으며, 같은 측정 결과로 재시도할 수 있습니다.
- `/api/status`의 `soundTimestampMs`와 `soundAgeMs`는 측정 수집의 중복·오래된 입력 검사용이며, 소리 에너지는 소수 6자리로 전달됩니다. 이 기능에는 새 펌웨어와 웹을 함께 적용해야 합니다.

실제 보드 검증에서는 측정 후 재부팅 복원, 재측정 교체, 공장 초기화 삭제, 저장 실패, 팬·LED가 센서에 미치는 영향을 확인해야 합니다.

환경 기준 관련 호스트 테스트: `bash scripts/test-environment.sh` (C++ 컴파일러, Node.js, 프론트엔드 의존성 필요). 실제 Zephyr settings 호출을 모의 저장소로 대체하여 메모리 코드와 알고리즘 소스를 검증하며, 실물 플래시의 쓰기·전원 복원 검증을 대신하지 않습니다.
