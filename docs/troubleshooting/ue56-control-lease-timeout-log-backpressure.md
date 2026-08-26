# UE 5.6 control lease timeout과 서버 로그 backpressure 해결 사례

## 증상

PIE 주행 중 조작이 멈추고 Unreal 로그에 다음 경고가 반복됐다.

```text
LogSimCoreClient: Warning: Connection closed (1008, clean=true): control lease timed out; reconnect required
```

서버에는 command queue-age 거부, 약 250~480ms command timeout과 수천 건의
`[Timing] tick overrun count=...`가 함께 쌓였다.

## 원인 판별

이번 사례는 Unreal heartbeat 구현 누락이 아니었다.

- Unreal game frame은 관측 구간에서 약 66Hz로 계속 진행했고 같은 component tick의
  20Hz heartbeat 경로도 살아 있었다.
- 반면 수신 WorldState는 59~60Hz에서 약 40Hz, 이후 약 25Hz로 먼저 저하됐고 최대
  packet gap과 local age가 250ms를 넘었다.
- C++ host는 물리 tick, WebSocket read/write와 동기식 `std::cerr`를 하나의
  `io_context` thread에서 처리한다.
- 자동화가 서버를 console pipe에 연결해 둔 뒤 출력을 계속 소비하지 않는 상태에서
  매 overrun을 한 줄씩 쓰면 pipe buffer가 찰 수 있다. 동기 출력에 막힌 같은 thread는
  heartbeat도 제때 읽지 못하고, 다시 실행된 순간 command를 queue-age로 거부한 뒤
  timeout을 발생시킨다.

즉 `overrun 출력 증가 → single I/O thread 정체 → command/state 지연 → timeout과 추가
overrun`의 양의 피드백이었다.

## 수정

1. tick overrun은 매 건 출력하지 않고 최대 1초에 한 번 `total`과 그 구간의 `delta`를
   요약한다.
2. 자동화용 `run_landscape_server.ps1 -Background`는 숨김 process로 실행하고
   stdout/stderr를 저장소의 ignored `runtime_logs/` 파일에 redirect한다.
3. control lease를 두 단계로 나눈다.
   - 250ms: throttle 해제, full brake·handbrake `SafeStop`; session/socket 유지
   - 1초: 그때까지 fresh command가 없을 때만 session retire와 WebSocket 1008 close
4. soft SafeStop 중에도 기존 100ms queue-age, sequence와 session 검사는 유지한다.
   같은 session의 신선한 command만 재연결 없이 다시 arm할 수 있다.

단순히 timeout 전체를 1초로 늘리지 않았다. 그러면 실제 제어 단절 때 안전 제동도
1초 늦어지기 때문이다.

## Windows 실행과 확인

자동화 또는 오래 유지할 서버는 저장소 루트에서 다음처럼 실행한다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities -Background
```

launcher가 PID와 두 로그 파일 경로를 출력한다. 시작 확인은 다음 기준을 사용한다.

```powershell
Get-Process -Name simcore_publisher
netstat -ano | Select-String ':9000'
Get-Content .\runtime_logs\simcore-landscape-*.stdout.log -Tail 30
Get-Content .\runtime_logs\simcore-landscape-*.stderr.log -Tail 30
```

정상 시작 로그에는 차량 config checksum, Landscape package checksum,
`ground_triangles`, `static_colliders`와 `WS binary :9000`이 있어야 한다. 현재 tracked
Landscape에서 `static_colliders=0`은 marker를 아직 bake하지 않은 상태와 일치한다.

사용자가 콘솔을 직접 열어 계속 보고 있는 foreground 실행은 그대로 사용할 수 있다.
Codex·CI·background task처럼 pipe 소비가 보장되지 않는 경우에는 `-Background`를 쓴다.

## 회귀 기준

- 250ms를 조금 넘긴 command 공백: SafeStop, socket close 0회
- 같은 session의 fresh command: 즉시 control 복구
- 오래된 FIFO command: SafeStop 해제 실패
- 1초를 넘긴 공백: session retire와 close callback 정확히 1회
- Windows Release CTest 11/11 통과
- 차량·설정·control lease·simulation host 4종을 각 20회 반복해 80/80 통과
- background runtime smoke에서 `:9000` listen, stdout/stderr 파일 분리와 overrun 1Hz
  이하 요약 확인

## 재발 시 확인 순서

1. Unreal의 game FPS와 WorldState rate를 분리한다. game FPS는 정상인데 state rate가
   먼저 떨어지면 서버 I/O thread 정체를 우선 의심한다.
2. stderr에서 `tick overruns total/delta`, `queue-age`, soft timeout과 hard timeout의
   시각 순서를 확인한다.
3. 1008이 발생했다면 직전 command 공백이 실제로 1초를 넘었는지 확인한다.
4. server log 파일이 정상 증가하는데 process CPU/state가 멈추면 별도 physics thread나
   비동기 structured logger 분리를 다음 단계로 검토한다.
