# 빌드·실행·하드웨어

## 구성

- 펌웨어: nRF54LM20 DK, NCS 3.4.0, Zephyr RTOS, C++17
- 웹: React 19, TypeScript, Vite 7
- `src/peripheral`: 센서·출력 드라이버
- `src/common`: 장치 모델과 제어 알고리즘
- `src/system`: 실행 주기, 설정 저장, Matter, HTTP 서버
- `frontend/src`: 사용자 화면
- `tests`: 호스트·프론트엔드 검사

## 펌웨어

Nordic SDK와 툴체인, Node.js와 npm이 필요합니다. 기본 경로는 `scripts/build.sh`에 정의되어 있습니다.

```sh
# 처음 빌드하거나 빌드 구성이 달라졌을 때
./scripts/build.sh --pristine
./scripts/flash.sh

# 증분 빌드 및 앱만 설치
./scripts/build.sh
./scripts/flash.sh --app-only
```

전체 설치는 MCUboot → Matter factory data → 앱 순서입니다. 앱만 설치하려면 MCUboot와 factory data가 이미 있어야 합니다.

경로·대상은 `NCS_ROOT`, `TOOLCHAIN_DIR`, `BUILD_DIR`, `BOARD` 환경 변수로 지정합니다. 기본 보드는 `nrf54lm20dk/nrf54lm20b/cpuapp`입니다. VS Code 작업도 같은 스크립트를 사용합니다.

기본 빌드는 시연 설정을 사용합니다. 일반 설정으로 빌드하려면 다음과 같이 실행합니다. 각 설정값은 [자동 제어](control.md)에 정리되어 있습니다.

```sh
DEMO_DEFAULTS=OFF ./scripts/build.sh --pristine
```

펌웨어 빌드는 웹 production 빌드를 실행하고 정적 자산을 펌웨어에 포함합니다.

## 웹

```sh
cd frontend
npm ci
npm run dev:mock  # 보드 없이 모의 API 사용
npm run build    # 타입 검사 및 production 빌드
```

실제 보드를 사용하려면 `frontend/.env.example`을 참고해 `frontend/.env`에 `API_TARGET=http://[보드 IPv6 주소]`를 설정하고 `npm run dev`를 실행합니다. 기본 포트는 5173입니다. 대상이 없으면 기기 설정을 불러올 수 없어 연결 안내가 표시됩니다.

보드 웹 화면은 HTTP 80 포트에서 제공합니다. LAN에서는 Thread 보더 라우터가 광고한 OMR IPv6 주소를 사용합니다. 링크 로컬·mesh-local·RLOC 주소는 일반 LAN 접속용 주소가 아닙니다.

## 배선

| 장치 | 연결 |
| --- | --- |
| BH1750 | SDA `P1.12`, SCL `P1.13`, I2C 주소 `0x23` |
| MP34DT01 | PDM CLK `P1.14`, DATA `P1.11` |
| HC-SR501 | 신호 `P1.10` |
| WS2812B 12 LED | DI `P3.00`, 전원 5 V, 공통 GND |
| 팬 | PWM `P3.02` |

WS2812B의 DO는 다음 LED 연결용입니다. 핀 설정은 `boards/nrf54lm20dk_nrf54lm20b_cpuapp.overlay`를 기준으로 합니다.

## 검사

프로젝트 루트에서 실행합니다.

```sh
bash scripts/test-host.sh
node tests/ui/mock-api.test.mjs
node tests/ui/child-control.test.mjs
```

브라우저 검사는 Playwright와 Chromium이 필요합니다. Chrome을 사용하려면 검사 명령에 `PLAYWRIGHT_CHANNEL=chrome`을 지정합니다. 별도 터미널에서 모의 서버를 실행한 뒤 검사합니다.

```sh
cd frontend
npm run dev:mock -- --port 5174
```

```sh
node tests/ui/onboarding-smoke.cjs
```

호스트·모의 API 검사는 실제 센서, 팬 회전, LED 발광색, Thread·Matter 연결을 검증하지 않습니다. 보드에서는 측정값 갱신, 자동 완화·복귀, 직접 제어, 설정 복원을 확인합니다.
