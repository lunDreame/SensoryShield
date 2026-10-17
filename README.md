# SensoryShield

nRF54LM20 DK 기반 감각 환경 제어 장치입니다. 주변 밝기·소리·움직임을 감지하고, 개인 설정에 따라 조명과 팬을 조절합니다. Thread 네트워크에서 웹 화면과 Matter 연동을 제공합니다.

## 시작하기

펌웨어 빌드 및 설치:

```sh
./scripts/build.sh --pristine
./scripts/flash.sh
```

프론트엔드 실행:

```sh
cd frontend
npm ci
npm run dev:mock
```

## 문서

- [빌드·실행·하드웨어](docs/development.md)
- [자동 제어와 설정](docs/control.md)
- [REST API](docs/api.md)
