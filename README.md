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

REST API는 `/api/status`, `/api/light`, `/api/fan`, `/api/mode`, `/api/profile`, `/api/diagnostics`, `/api/factory-reset`을 제공합니다. `/api/light`는 `power`, `brightness`, `cct`, `rgbMode`, `red`, `green`, `blue`를 받을 수 있습니다. Matter ColorControl은 Level, ColorTemperature, Hue/Saturation 명령을 지원합니다. factory reset 엔드포인트는 확인 요청이 있는 경우에만 동작합니다.

## 현재 하드웨어 매핑

- BH1750 I2C: SDA `P1.12`, SCL `P1.13`
- MP34DT01 PDM: CLK `P1.14`, DATA `P1.11`; 16 kHz mono, 100 ms window
- HC-SR501 PIR: `P1.10`
- WS2812B 12 LED ring: `DI` -> `P3.00` 출력 가능 GPIO 패드, `5V` -> 5 V 전원, `GND` -> 공통 GND, `DO` -> 다음 LED 체인 연결용 또는 미사용
- Fan PWM: `P3.02`

WS2812B `DI`는 보드의 출력 가능한 GPIO 패드에 연결합니다. 현재 펌웨어는 `P3.00`을 WS2812B 데이터 파형 출력으로 사용합니다.

WS2812B 조명은 밝기 디밍, RGB 색상, 백색 색온도 제어를 지원합니다.

Thread REST 접속에는 보더 라우터가 LAN으로 광고한 OMR IPv6 주소를 사용합니다. 링크 로컬, mesh-local, RLOC 주소는 일반 LAN 클라이언트에서 직접 접근할 수 없습니다.
