# ADR-014: 차량 신호는 서버 simulation clock을 따른다

- 날짜: 2026-08-31
- 상태: 차선·신호 구현 단계에 적용한 설계 결정
- 대체: 아키텍처의 기존 목표 `Unreal SignalController` 신호 시간 권한

## 배경과 결정

기존 문서는 Unreal에서 교통 신호를 계산하는 목표를 두었다. 이번 차선·신호 단계는
차량 reset/고정 tick과 신호의 시간 기준이 달라지는 문제를 피하기 위해 C++에서
차량 신호 주기를 계산하고 Unreal은 수신·표시하도록 구현한다. 사용자에게 구현 중
변경 이유를 알렸으며, 이를 별도의 사용자 설계 승인 기록으로 표현하지 않는다.

Unreal은 도심 도로 원본·실제 collision과 방향 차선/정지선 연결을 저작·검증한다.
C++는 immutable JSON의 source collision checksum·topology·ground footprint를
검증하고 simulation nanoseconds로 신호 위상을 계산한다. 최초 `virtual_city_v1`의
30초 고정 schedule은 호환 이력으로 보존하고, `signal_city_v2`부터는 sidecar에 저작한
controller별 phase plan과 offset을 같은 simulation clock으로 평가한다. WorldState의 차량
pose/Health/신호에 같은 sequence·map/play identity가 적용된다. Unreal의 local time은
100ms stale를 판정해 녹색을 취소하는 데만 사용하고 다음 색을 예측하지 않는다.

## 결과와 범위

- 새 PIE는 차량과 신호 시간을 함께 reset한다. 같은 PIE 재연결은 위상을 중복 reset하지 않는다.
- 전환 사이 all-red, inactive lease/EStop/지면 identity 불일치 시 all-red를 적용한다.
- lane/signal 교체는 off-thread load와 tick-boundary install이며 수동운전·지면 hot reload를 막지 않는다.
- 프로토콜은 선택 `traffic-signals.v1`과 기존 schema v2 필드 의미를 보존한다. head의
  `controller_id`는 additive 필드이며 기존 v1 소비자의 해석을 바꾸지 않는다.
- 최초 `virtual_city_v1`은 한 교차로/2그룹/3개 차량 head와 고정 schedule이다. 현재
  `signal_city_v2`는 방향 차선 42개, 물리 차량 head 8개, 독립 controller 2개와 데이터 기반
  phase plan/offset을 갖는다.
- Unreal은 controller와 head의 authoritative 결과를 표시만 하고 local phase나 offset을
  계산하지 않는다.
- 최종 다중 NPC 신호 준수, 보행 신호·보행자 AI와 사용자 PIE 인수는 후속이다.
- Unreal 교통 표현/입력/센서 책임, 자체 차량 물리·외부 물리 SDK 미사용은 유지한다.

구체 schema·한계·재현 명령은 [차선·신호 문서](../traffic_network_signals.md)를 따른다.
