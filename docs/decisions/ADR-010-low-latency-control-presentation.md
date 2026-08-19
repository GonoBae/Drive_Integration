# ADR-010: 로컬 수동운전 저지연 제어와 제한된 상태 예측

## 상태

- 결정: 채택
- 결정일: 2026-08-19
- 적용 범위: Windows 로컬 Unreal 수동입력 → C++ SimCore → Unreal 상태 표시
- 물리 권한: C++ SimCore 유지
- transport: WebSocket binary + Protobuf 유지

## 배경

로컬 PC에서 Unreal 키 입력을 C++가 계산한 뒤 다시 Unreal에 표시하는 수직 절단은 동작했지만, 차량이 키보다 늦게 따라오고 간헐적으로 끊겨 보였다. loopback 측정에서 command 전송 후 첫 speed 변화는 약 35.62ms였으나, state 도착 간격은 p95 30.05ms·최대 49.82ms까지 벌어졌다. Unreal의 `VInterpTo/RInterpTo` 추종 보간은 여기에 약 80~100ms의 시각 지연을 추가했다.

원인은 WebSocket의 대역폭이 아니라 다음 세 지점이었다.

- Windows 기본 timer granularity에서 60Hz `steady_timer` wake-up이 15~30ms 단위로 흔들림
- 최신 authoritative pose를 설정값상 약 80~100ms의 추가 지연을 만들 수 있는 추종 보간에 넣어 입력 반응이 늦어짐
- UE 5.6의 약 30Hz libWebSockets service보다 빠른 60Hz command heartbeat가 내부 FIFO에 누적되어 실제 키 변화도 오래된 heartbeat 뒤에서 대기함

WebSocket handshake 중 state frame이 HTTP 101보다 먼저 쓰이던 별도 오류도 발견했다. 이 문제는 accept 완료 전 write를 금지하는 세션 상태로 해결하고 회귀 시험을 추가했다.

## 결정

### C++ host

- Windows 실행 중 `timeBeginPeriod(1)`로 1ms timer resolution을 요청하고 RAII로 `timeEndPeriod(1)`을 보장한다.
- 60Hz 절대 deadline `SimulationClock`과 고정 `dt`는 유지한다.
- TCP `no_delay`를 사용한다.
- state write queue는 진행 중 1개와 최신 대기 1개만 유지하는 latest-wins 정책을 사용한다.
- WebSocket accept 완료 전에는 binary state를 전송하지 않는다.

### Unreal 입력

- 동일 frame의 throttle, brake, steering callback을 최신 command 하나로 합친다.
- gamepad noise는 deadzone 0.02와 변화 epsilon 0.005로 거르고, 변화 command는 최대 30Hz로 제한한다.
- 값이 유지될 때는 20Hz heartbeat로 250ms command timeout lease를 유지한다.
- 연결마다 고유 `session_id`를 만들고 sequence를 1부터 다시 시작한다.
- 250ms timeout에서는 서버가 기존 session과 socket을 폐기하고, Unreal은 기본 0.5초 뒤 새 session으로 재연결한다.
- Windows에서는 libWebSockets event-loop service를 사용해 새 입력이 socket thread를 즉시 깨우도록 한다.
- event-loop 미지원 플랫폼의 polling fallback은 240Hz로 설정한다.
- Editor PIE에서는 background CPU throttling을 꺼 game tick 저하로 heartbeat가 250ms를 넘지 않게 한다.

### Unreal 상태 표시

- 마지막 state의 로컬 수신 시각을 저장한다.
- body-frame 선속도를 heading 기준 Unreal world 속도로 변환해 pose를 render frame까지 예측한다.
- yaw/roll/pitch 각속도도 같은 제한 시간 동안 자세 예측에 사용한다.
- dead reckoning은 최대 50ms만 허용하며 이후에는 마지막 제한 pose를 유지한다.
- C++ authoritative state를 변경하거나 Unreal Chaos로 다시 물리 계산하지 않는다.

## 측정 결과

조건은 Windows Release host, `127.0.0.1:9000`, WebSocket compression 비활성 Python probe, 240개 state 표본이다.

| 지표 | 개선 전 | 개선 후 |
|---|---:|---:|
| state 간격 p95 | 30.05ms | 17.08ms |
| state 간격 최대 | 49.82ms | 17.20ms |
| 25ms 초과 interval | 26/239 | 0/239 |
| command 전송→첫 speed 변화 | 약 35.62ms | 약 27.32ms |

이 결과는 host와 transport의 loopback 기준이다. Unreal input sampling, render frame, 디스플레이 지연을 모두 포함한 end-to-end 수치는 packaged build에서 추가 측정한다.

추가 PIE 진단에서는 WorldState가 대체로 58~61Hz, `server_age` 약 0~27ms, `local_age` 약 1~31ms로 유지됐지만 실제 키 지연은 계속 증가했다. UE 5.6 송신 FIFO의 60Hz 생산/약 30Hz 소비 불균형을 수정한 뒤 30초 이상 반복 조작에서 사용자가 누적 지연 해결을 확인했다. 상세 재현과 진단 순서는 [UE 5.6 WebSocket 입력 지연 누적 해결 사례](../troubleshooting/ue56-websocket-growing-input-delay.md)에 기록한다.

## 결과와 비용

- 60Hz state 간격이 한 tick 근처로 안정되어 시각적 hitch가 줄어든다.
- 변화 입력은 latest-wins로 합쳐 최대 약 33ms 안에 전송하며, 축별 callback과 gamepad noise가 여러 FIFO 항목을 만들지 않는다.
- 20Hz heartbeat와 event-loop service로 command FIFO가 시간에 따라 증가하지 않는다.
- 제한된 예측으로 패킷 사이 render frame을 이어 주면서 무제한 외삽을 방지한다.
- 1ms timer resolution은 서버 실행 중 전력 사용과 wake-up 빈도를 높일 수 있으므로 수동운전 runtime에서만 유지한다.
- sudden collision이나 teleport에서는 최대 50ms 예측 오차가 보일 수 있으며 다음 authoritative state가 즉시 교정한다.

## 재검토 조건

다음 중 하나가 반복되면 interpolation buffer, UDP/IPC 또는 별도 physics thread를 재검토한다.

- packaged loopback에서 state age p95가 33ms를 초과
- LAN에서 TCP head-of-line blocking으로 오래된 state가 최신 state를 막음
- 다중 엔티티 추가 후 state interval p95가 18.5ms를 초과
- collision·teleport 시 50ms 예측 보정이 눈에 띄게 불안정
- 30분 시험에서 tick overrun이 지속적으로 누적

## 검증

- C++ MSVC Release 빌드 통과
- C++ 회귀시험은 최초 Windows에서 5종, 2026-08-19 전체 리팩터링 후 macOS에서 6종 통과
- handshake 전 broadcast 회귀 시험 통과
- Unreal 5.6 Game target 빌드 통과
- Unreal 5.6 Editor target 빌드·DLL 링크 통과
- 실제 PIE 30초 이상 반복 조작에서 누적 입력 지연 해소 확인

위 Windows·PIE 결과는 최초 저지연 수정 기준이다. 종료 재검토에서 추가한 session/queue-age와 최대 30Hz coalescing 변경은 macOS C++·Python 검증까지 통과했으며, 목표 Windows에서 Unreal Game/Editor target과 end-to-end 입력 지연을 다시 측정해야 한다.

## 참고 자료

- [Microsoft: timeBeginPeriod](https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)
- [ADR-005: WebSocket binary + Protobuf](./ADR-005-realtime-transport-protocol.md)
