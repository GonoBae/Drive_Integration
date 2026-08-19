# UE 5.6 WebSocket 입력 지연 누적 해결 사례

## 증상

- PIE 시작 직후보다 시간이 지날수록 W/A/S/D 반응이 늦어진다.
- 키를 바꿔도 차량이 즉시 반응하지 않고, 오래 실행할수록 지연이 커진다.
- C++ 서버 로그에서는 입력을 받은 뒤 다음 물리 tick까지 5~17ms로 보여 원인이 서버 밖에 있음을 알 수 있었다.
- Unreal은 WorldState를 대체로 58~61Hz로 받고 `server_age`도 약 0~27ms였으므로 상태 수신 적체도 주원인이 아니었다.

## 진단에서 놓치기 쉬운 점

초기 측정은 `서버가 ControlCommand를 받은 시점 → 물리 적용 → WorldState 수신`만 포함했다. Unreal 내부 송신 FIFO에서 명령이 기다린 시간은 포함하지 않았다. 따라서 서버 도착 이후 수치가 빨라도 실제 키 입력의 end-to-end 지연은 계속 증가할 수 있었다.

## 근본 원인

UE 5.6의 libWebSockets 경로는 다음 특성을 가진다.

1. `IWebSocket::Send()`는 메시지를 내부 FIFO `SendQueue`에 추가한다.
2. 기본 WebSocket service thread 주기는 `0.0333`초, 약 30Hz다.
3. writable callback 한 번에 FIFO의 메시지 하나를 전송한다.
4. 기존 클라이언트는 입력 유지 heartbeat를 60Hz로 추가했다.

생산 60Hz, 소비 약 30Hz이므로 큐가 초당 약 30개씩 증가했다. 입력 변화 시 즉시 `Send()`를 호출해도 이미 쌓인 heartbeat 뒤에 들어가므로 실제 키 명령은 시간이 갈수록 늦게 서버에 도착했다.

별도로 Unreal Editor의 기본 `Use Less CPU when in Background`가 켜지면 PIE game tick이 약 3FPS까지 내려간 구간도 관측됐다. 이때 heartbeat 간격이 약 330ms로 벌어져 서버의 250ms command timeout과 SafeStop이 반복될 수 있었다.

## 수정

### WebSocket service

`Config/DefaultEngine.ini`에서 Windows가 지원하는 event-loop service를 사용한다.

```ini
[WebSockets.LibWebSockets]
bPollService=False
ServiceTimeoutMs=1000
ThreadTargetFrameTimeInSeconds=0.004166667
ThreadMinimumSleepTimeInSeconds=0.0
```

- event-loop 모드에서는 새 입력이 socket service를 즉시 깨운다.
- `ThreadTargetFrameTimeInSeconds`는 event-loop를 지원하지 않는 플랫폼을 위한 240Hz fallback이다.

### ControlCommand 전송 정책

- 입력 변화: 즉시 전송
- 입력 유지 heartbeat: 60Hz에서 20Hz로 변경
- 20Hz heartbeat는 250ms lease를 충분히 갱신하면서 UE 기본 30Hz service에서도 FIFO가 증가하지 않는다.
- 물리와 WorldState는 계속 60Hz를 유지한다. 20Hz는 상태 주기가 아니라 동일 입력을 반복하는 lease heartbeat 주기다.

### Editor focus

`Config/DefaultEditorSettings.ini`에서 로컬 PIE 중 background CPU throttling을 끈다.

```ini
[/Script/UnrealEd.EditorPerformanceSettings]
bThrottleCPUWhenNotForeground=False
```

## 검증 결과

- UE 5.6 Editor target 빌드 및 DLL 링크 통과
- C++ Release 빌드와 CTest 4/4 통과
- 실제 PIE에서 30초 이상 반복 조작 후 사용자가 누적 지연 해결과 체감 개선을 확인
- UDP나 별도 relay로 교체하지 않고 WebSocket binary + Protobuf 구조 유지

## 재발 방지 기준

- periodic producer rate는 transport의 보장된 consumer rate보다 낮아야 한다.
- 제어 명령은 가능하면 FIFO 무제한 적재가 아니라 latest-wins 또는 coalescing 정책을 사용한다.
- 지연 측정은 `키 입력 생성 → Unreal 송신 큐 → 서버 수신 → 물리 적용 → Unreal 상태 수신 → render` 전 구간을 포함해야 한다.
- 향후 protocol에 client 생성 시각 또는 command revision acknowledgement를 추가해 송신 큐 대기까지 자동 계측한다.
- `command timeout` 로그에는 실제 command age를 남겨 frame hitch와 안전 정지를 구분한다.

## 빠른 확인 순서

1. 지연이 시간에 비례해 증가하는지 확인한다. 증가한다면 FIFO 생산·소비율을 먼저 의심한다.
2. 서버의 입력 수신 후 물리 적용 시간과 Unreal `server_age/local_age/max_gap`을 분리해서 본다.
3. PIE frame hitch와 `SafeStop` 로그가 같은 시각에 발생했는지 확인한다.
4. heartbeat 주기와 WebSocket service 주기를 비교한다.
5. 수정 후 짧은 탭 입력과 30초 이상 장시간 입력을 모두 시험한다.
