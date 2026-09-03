# Unreal Bake 후 조작 불가·서버 재시작 해결 사례

## 증상

- SimCore와 PIE가 정상 연결된 상태에서 Landscape 또는 static collider를 다시 Bake하면
  차량 조작이 멈춘다.
- Unreal은 새 로컬 `manifest.cfg` checksum을 읽지만 실행 중 서버는 이전 checksum을 계속
  발행한다.
- checksum/reset gate가 일반 `ControlCommand`를 거부하므로 서버 프로세스를 재시작해야만
  다시 움직였다.

이때 control 거부 자체는 잘못이 아니다. 서로 다른 ground/collision snapshot으로 Unreal과
C++가 계속 진행하지 않게 하는 fail-closed 동작이다. 문제는 host가 MapPackage를 시작할
때 한 번만 읽는 운영 계약이었다.

## 원인

Exporter는 다음 안전 순서로 package를 저장한다.

1. 새 header-only `ground_surface.csv` sentinel, `ground_heightfield.bin`,
   `static_colliders.csv`를 임시 파일에 완성한다.
2. 세 payload를 교체한다.
3. 실제 raw bytes checksum을 계산한다.
4. `manifest.cfg`를 마지막에 commit한다.

기존 host에는 4번 이후의 변경을 감지하는 수명주기가 없었다. 따라서 파일 저장은
원자적이었지만 실행 중 물리 snapshot은 갱신되지 않았다.

## 수정

### Background 검증

`MapPackageHotReloader`가 실행 중인 package의 `manifest.cfg`를 250ms 주기로 감시한다.
변경 후보는 simulation thread 밖에서 다음 항목을 모두 검증한다.

- strict manifest format과 map identity
- 선언된 ground/static payload 존재와 strict parsing. heightfield가 선언되면 sentinel에
  triangle row가 없는지도 검증
- 선언된 모든 payload의 실제 checksum
- 검증 마지막 시점의 manifest가 처음 읽은 후보와 같은지

작성 중, 누락, checksum mismatch 또는 parsing 실패 후보는 적용하지 않는다. 현재
ground/collision snapshot은 그대로 유지한다. 여러 완전한 후보가 빠르게 연속 도착하면 가장
최근 후보만 적용 대상으로 남긴다.

### 60Hz tick 경계 원자 교체

검증된 후보는 고정 simulation tick 시작점에서만 적용한다.

- ground provider
- static collision world
- authoritative map checksum
- vehicle spawn/state
- simulation clock cadence
- control lease와 play lifecycle
- opt-in runtime entity

위 상태를 한 경계에서 교체·reset한다. 한 tick 안에서 새 ground와 이전 collision이 섞이거나
새 checksum에 이전 차량 pose가 남지 않는다. 이전 WebSocket과 session은 fence한다.

### Unreal 자동 재연결

서버가 기존 socket을 닫으면 Unreal client는 재연결 전에 로컬 manifest와 payload를 다시
검증한다. checksum이 이전 연결과 달라졌다면 새 `PlaySessionId`를 만들고, 새 서버
`WorldState` checksum이 일치한 뒤 `SimulationReset`과 첫 control을 보낸다.

따라서 payload가 실제로 바뀐 정상 Bake에서는 짧은 disconnect/reconnect가 보인다. 이는
실패가 아니라 stale session을 새 physics snapshot으로 넘기지 않는 안전 절차다.

## 정상 로그

서버 PID가 바뀌지 않은 상태에서 다음 순서를 확인한다.

```text
[MapReload] verified candidate ... queued for tick boundary
[MapReload] applied at tick boundary ... previous=... current=...; vehicle and lifecycle reset
```

Unreal에서는 잠깐 연결이 끊긴 뒤 자동으로 다시 연결되고 차량이 새 package spawn에서
조작돼야 한다.

- 같은 내용을 다시 Bake해 payload checksum이 같다면 reload 로그와 reconnect가 없는 것이
  정상이다.
- invalid/작성 중 후보가 보이면 서버는 이를 거부하고 이전 map에서 계속 실행해야 한다.
- process-lifetime E-stop latch 해제는 별도 계약이므로 여전히 서버 재시작이 필요하다.

## 검증 결과

- startup/watch 경쟁과 manifest-last invalid→valid 복구
- 같은 후보 1회 전달과 latest verified candidate 우선
- tick-boundary ground/static/checksum 원자 교체
- vehicle·clock·lease·runtime entity reset
- 이전 checksum과 stale play/session control 거부
- 새 checksum의 fresh Reset 이전 control 거부, Reset 이후 허용
- 같은 PID `11152`에서 historical fixture를 현재 Landscape checksum
  `fnv1a64:b95788d7e9fcaf21`로 교체하는 runtime smoke
- 실제 서버 PID `6692`가 legacy package로 시작한 뒤 UE가 Bake한 100cm
  `ground_heightfield.bin` 후보를 검증하고 checksum `fnv1a64:b8b0a6ccd89614de`로
  tick-boundary 교체. 로그의 sample/cell은 257,556/256,542로 payload header와 일치

2026-08-28 최종 Windows Release CTest는 13/13, 신규 heightfield query와 runtime reload는
각 50/50, WebSocket 회귀는 100/100 통과했다. UE 5.6 Editor와 Game build도 통과했다.

## 수동 확인

1. Landscape 서버와 PIE를 실행해 차량이 움직이는지 확인한다.
2. 서버 PID를 기록하고 PIE를 정지한다.
3. 현재 baseline은 이미 `ground_heightfield.bin`, 100cm spacing과
   `ground_format=packed_heightfield_v1` 적용을 통과했다. Landscape를 눈에 보이게 조금
   수정하거나 marker를 추가하고 다시 Bake한다.
4. 같은 PID의 `[MapReload]` 두 로그를 확인한다.
5. PIE를 실행하거나 자동 재연결을 기다린 뒤 차량이 새 spawn에서 조작되는지 확인한다.
6. marker를 추가했다면 `static_colliders>0`과 벽·커브 비관통도 확인한다.

현재 tracked full-bounds Landscape는 `507×508` lattice, 257,556 samples/256,542 cells,
1m step, 6,177,368-byte `SIMGHF1` payload이며 checksum은
`fnv1a64:b8b0a6ccd89614de`다. 첫 형식 전환과 same-PID apply는 완료됐다. 위 절차는 Sculpt
변경 또는 marker 추가로 checksum이 다시 바뀔 때 Unreal의 disconnect→reconnect→새
PlaySession Reset까지 확인하는 남은 end-to-end gate다.
