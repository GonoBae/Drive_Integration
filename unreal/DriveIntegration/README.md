# Unreal 클라이언트

Unreal Engine 5.6 프로젝트입니다. 차량 물리·교통 상태는 C++ SimCore 서버에서 계산하고,
클라이언트는 입력, 표시, 카메라, HUD와 에디터의 지면·충돌 측정을 담당합니다.

## 실행

저장소 루트에서 다음 명령으로 서버를 실행한 뒤 이 폴더의 `DriveIntegration.uproject`를 엽니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background
```

기본 맵은 `/Game/SignalCity/Maps/L_SignalCity`입니다. Play 후 연결 상태가 `Active`인지 확인합니다.
저장된 맵을 시험할 때 Ground Bake를 다시 할 필요는 없습니다.

- `W/S/A/D`: 가속·제동·후진·조향
- `Space`: 사이드 브레이크
- `1~4`: 플레이어 차종 선택
- `V`: 카메라 전환, 마우스·휠: 회전·거리
- `F3`: 충돌·접지 디버그
- `R`: 주행 기록, `F6`: 시각 재생

## 문서와 검증

- [전체 실행 방법과 조작법](../../README.md)
- [Signal City 실행·지도·신호 확인](../../docs/guides/signal_city_quickstart.md)
- [자동 검사와 수동 인수의 구분](../../docs/guides/core_validation.md)
- [Windows 패키지 제작](../../docs/guides/windows_package.md)
- [현재 검증 현황과 남은 인수](../../docs/status.md)

이전 Virtual City와 Landscape 자료는 회귀 시험·지도 전환 확인에 사용하므로 보존합니다.
현재 기능·제약·검증 수치는 위 문서를 기준으로 봅니다. 오래된 맵 수치와 시험 결과를 이 파일에
중복 관리하지 않습니다.

`Source`, `Config`, `Content`와 프로젝트 설정을 Git으로 관리합니다.
`Binaries`, `Intermediate`, `Saved`, 캐시는 생성물이므로 제외합니다.
