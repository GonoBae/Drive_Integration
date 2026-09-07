# NPC 차선 추종·신호 준수 기반

## 현재 적용 범위(2026-09-03)

Signal City는 NPC 4대·보행자 8명을 사용하며, NPC에는 목적지 자동 선택·같은 목적지로의
우회·저작된 같은 방향 차선 변경을 추가했다. 현재 설정과 안전 경계는
[NPC 내비게이션](./npc_navigation.md), 실행은 [Signal City 빠른 시작](./signal_city_quickstart.md)을 따른다.
NPC는 여전히 kinematic follower이며 Ego와 유한질량 충돌 반작용을 주고받는다.
보행자는 지정 횡단보도만 사용한다. 현재 확장의 검증 결과는 9/3 작업일지에서 구분한다.

## 8/31 최초 단일 NPC 구현 이력

아래 수치·v1 설정·당시 미지원 항목은 최초 구현 기록이다. 현재 Signal City의 고정 경로
여부·충돌 반작용·NPC 수를 설명하는 기준으로 사용하지 않는다.

2026-08-31 추가. 기존 신호 기반 위에 **NPC 차량 1대의 주행 동작**을 연결했다.
이것은 R1 목표인 NPC 3~4대·보행자 6~8명의 완료가 아니며, 최종 수동 인수도 별도다.
검증 결과는 [작업일지](./worklogs/2026-08-31.md)의 NPC 절을 따른다.

8/31 당시 자동 검증: C++ **18/18**, Unreal **28/28**, Editor/Game 빌드,
실제 NPC **120.43초·7,225 state** 주행/신호/정지/새 Play 시험 통과. 사용자 PIE 인수는 대기다.

## 한 번에 확인하기

최신 서버와 Editor 빌드에서 `/Game/VirtualCity/Maps/L_VirtualCity`를 열고 Play한다.
이번 변경에는 **Bake가 필요 없다**. 서버 실행법은 [빠른 시작](./virtual_city_quickstart.md)을 따른다.

1. `Active` 상태에서 앞쪽 약 20m에 세단 1대가 나타나 서서히 출발하는지 확인한다.
2. 약 22km/h 이하로 NPC를 따라 남쪽→동쪽→북쪽→서쪽 코스를 돈다. 코너에서는 약 18km/h로 감속한다.
3. 북쪽 교차로에서 NPC의 적색/황색 진입 금지와 녹색 재출발을 확인한다.
   처음 출발부터 따라가면 북쪽 정지선에서 적색 대기를 관찰할 수 있다.
4. NPC 앞에 정차하면 간격을 남겨 멈추고, 길을 비우면 다시 출발하는지 확인한다.
   Ego를 출발점에 세워 두면 한 바퀴 가까이 돌아온 NPC가 뒤에서 기다린다.
5. 정지 시 바퀴가 멈추고, 주행 시 차속에 맞춰 회전하는지 확인한다. 새 Play는 NPC도 처음 위치로 되돌린다.
6. 기존 Ego 전진/후진·마우스 카메라·경사 접지·저중속 코너링을 함께 확인한다.
   **180km/h 전타 조향의 큰 슬립/과도 yaw는 아직 미해결**이다. NPC 추가가 이 문제 해결을 뜻하지 않는다.

Ego에는 자동 신호 제동을 넣지 않았다. 운전자가 직접 정지한다.

## 서버와 Unreal 책임

- Unreal: 저장 도로/지면/차선 저작, 입력, 서버에서 받은 NPC pose의 제한된 예측 표시와 세단·바퀴 외형.
- C++: 불변 MapPackage/TrafficNetwork 조회, 차선 진행·속도·신호 정지·장애물 정지 결정,
  authoritative NPC 상태와 Ego 충돌용 proxy 생성, WorldState 원자 발행.
- 외부 물리 SDK나 Unreal Chaos 차량 물리를 추가하지 않았다.

`NpcLaneFollower`는 3D polyline 거리 기반의 **kinematic 제어기**다. Ego의 타이어/서스펜션
동역학을 NPC에도 복제한 것은 아니다. NPC OBB는 기존 충돌 엔진에서 무한 질량 운동학적
장애물로 취급되므로 Ego가 밀어 움직이는 양방향 충돌, 충돌 파손, NPC 전복은 지원하지 않는다.
현재 NPC는 ground 높이와 yaw를 추종한다. NPC 차체의 물리적 pitch/roll·독립 서스펜션은 후속 범위다.

## 설정

기존 runtime cfg v1에 선택 키를 추가했다. 키가 없으면 NPC는 생기지 않는다.
가상 도심 전용 cfg에만 다음 값이 들어간다.

```ini
npc_route=260,270,280,290,200,210,220,230,240,250
npc_route_loop=true
npc_start_offset_m=96
npc_max_speed_mps=6
```

첫 lane 260의 ENU `(-76,0)`에서 96m 떨어진 `(20,0)`가 spawn이다.
runtime ID는 `1001`, collision proxy ID는 `lane-npc-1001`이다.
NPC OBB는 길이/폭/높이 `4.4×2.0×1.5m`, 중심은 ground+0.85m다.
시각 메시의 바닥은 ground에 맞추며 충돌 OBB 크기로 원본 차량 비율을 왜곡하지 않는다.

CLI `--npc-route none`으로 끌 수 있다. `--npc-route`, `--npc-loop`,
`--npc-start-offset`, `--npc-max-speed`가 cfg보다 우선한다.
legacy `--demo-entities`와 lane NPC는 ID 중복을 피하도록 동시에 켤 수 없다.
cfg 자체는 시작 시 읽으며 이 네 설정의 변경에는 서버 재시작이 필요하다.
지면 Bake·traffic JSON의 기존 hot reload와는 별개다.

## 제어·안전 계약

- route 최대 64개 lane ID, successor 연결과 마지막→처음 loop 연결을 검증한다.
  open route는 명시적 terminal에서 끝나야 한다. 임의 U-turn/새 경로 생성은 없다.
- 최고속도는 cfg와 현재/다가오는 lane 속도 제한의 최솟값이다.
  기본 가속 1.5m/s²·제동 3.0m/s², 차량 앞끝 2.2m·정지 여유 0.5m를 적용한다.
- 신호는 해당 접근 lane 끝을 넘어가는 진입을 제어한다. 미확인/적색/황색은 정지,
  유효한 녹색에서 앞끝이 이미 통과했다면 connector를 빠져나오도록 한다.
  큰 dt에서도 snapshot 녹색 유효 시간이 끝나면 새 녹색을 추측하지 않는다.
- 매 tick 경로 전방을 0.25m 간격으로 검사해 Ego·정적 OBB·다른 runtime proxy와 겹치는
  위치 앞에서 정지한다. 이는 보수적인 단일 차량 주행 가드이며 연속 시공간 충돌 증명,
  추월·차선 변경·교차로 우선권·예측 기반 다중 차량 협상은 아니다.
- 지면 query의 중심과 OBB 네 모서리를 확인한다. 지면 소실/유효하지 않은 입력/큰 dt는
  정지하며, 갑자기 나타난 장애물이나 안전 상태는 편안한 가감속보다 즉시 정지를 우선한다.
- tick 시작 proxy에 accepted end pose까지의 속도를 설정해 Ego 충돌을 계산한 뒤,
  같은 end pose를 WorldState로 발행한다. NPC 위치를 두 번 적분하지 않는다.
- reset 전·첫 control 전·soft/hard lease timeout·EStop에서는 NPC 진행을 멈춘다.
  새 Play는 spawn/속도/신호 시간 reset, 동일 Play 소켓 재연결은 진행을 보존한다.
- map 교체 시 이전 GroundQuery 참조를 먼저 폐기한다. traffic checksum이 map과 안 맞으면
  NPC를 제거하고, 정합된 network가 적용되면 재생성한다. spawn에 Ego/벽이 있으면 생성을 보류한다.
  무효 traffic 후보는 기존 검증 network를 덮어쓰지 않는다.
- Unreal 표시 actor는 `NoCollision`이며 local stale/연결 해제/reset/삭제된 ID에서 정리한다.
  100ms freshness는 로컬 수신 처리 시각 기준이지 종단 지연 상한 보장이 아니다.
- NPC wire에는 현재 조향/개별 접촉 정보가 없어 표시 휠은 고정 축에서 굴림만 적용한다.
  Ego의 독립 조향·접촉 표시와 달리 NPC 앞바퀴 조향/서스펜션 시각화는 후속이다.

## 자동 검증

```powershell
ctest --test-dir cpp/host/build -C Release --output-on-failure
python scripts/smoke_npc.py
```

`smoke_npc.py`는 포트 9000을 쓰지 않는 전용 child 서버를 생성한다. 고유 source nonce를
Hello에서 확인한 후에만 Reset/control을 보내며, 종료 시 자기가 만든 child만 정리한다.
실제 코스/신호를 따라 약 2분 주행하면서 route 정합·신호 정지/재출발·출발점 Ego 앞 정지,
위치 변화와 발행 속도 일치·lease 정지·새 Play reset을 확인한다. 원본 map/traffic 파일은
수정하지 않고 `runtime_logs/npc-smoke-*.jsonl` 및 서버 로그에 증거를 남긴다.

8/31 당시 남은 확장(보존 이력): NPC 3~4대·서로의 선행 차량 대응, 분기/terminal 수명주기,
보행자 6~8명·횡단 신호·상호작용, 사용자 PIE 인수와 목표 PC 성능/30분 안정성.
