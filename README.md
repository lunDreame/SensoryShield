# SensoryShield

SensoryShield는 nRF54LM20 DK 기반의 로컬 감각 환경 제어 펌웨어입니다. Zephyr RTOS와 C++17을 사용하며, 로컬 센서 처리, PWM 액추에이터 제어, 영구 앱 설정, USB ECM 네트워크, 로컬 HTTP REST API를 포함합니다.

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

펌웨어 빌드는 프론트엔드 production 빌드를 실행하고, 생성된 HTML, JavaScript, CSS를 압축해 펌웨어 이미지에 포함합니다. 보드에서는 USB ECM을 통해 `http://192.0.2.1/` 주소로 웹 UI를 제공합니다.

개발 서버는 5173 포트에서 실행되며 `/api` 요청을 보드의 같은 엔드포인트로 프록시합니다. 보드가 없으면 프론트엔드는 오프라인 미리보기 상태로 동작하고 장치 제어는 비활성화됩니다.

REST API는 `/api/status`, `/api/light`, `/api/fan`, `/api/mode`, `/api/profile`, `/api/diagnostics`, `/api/factory-reset`을 제공합니다. factory reset 엔드포인트는 확인 요청이 있는 경우에만 동작합니다.

## 현재 하드웨어 매핑

- BH1750 I2C: SDA `P1.12`, SCL `P1.13`
- MP34DT01 PDM: CLK `P1.14`, DATA `P1.11`; 16 kHz mono, 100 ms window
- HC-SR501 PIR: `P1.10`
- Warm PWM: `P3.00`
- Cool PWM: `P3.01`
- Fan PWM: `P3.02`

3.3 V 출력을 MCU 핀에 연결하기 전에 DK의 VDD/VDDIO와 브레이크아웃 보드의 신호 레벨을 확인해야 합니다.

USB VID/PID 값은 아직 Zephyr 테스트 기본값입니다. 제품 배포 전에는 제품에 할당된 값으로 교체해야 합니다.
