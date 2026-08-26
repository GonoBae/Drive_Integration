# Map packages

C++ SimCore와 Unreal이 같은 `map_enu` 충돌 snapshot을 사용하기 위한 버전형 데이터
경로다. R1의 현재 package 구조는 다음과 같다.

```text
<map_id>/
  manifest.cfg
  ground_surface.csv
  static_colliders.csv
  README.md
```

## `manifest.cfg`

현재 format version은 1이며 아래 다섯 key만 허용한다.

```ini
format_version=1
map_id=<package directory identity>
coordinate_frame=map_enu
collision_files=ground_surface.csv,static_colliders.csv
collision_checksum=fnv1a64:<16 lowercase hex digits>
```

`collision_checksum`은 선언된 순서대로 각 UTF-8 파일명, NUL, 파일 raw bytes, NUL을
FNV-1a 64로 누적한 실제 collision identity다. C++와 Unreal은 manifest 누락,
unknown/duplicate/missing key, unsafe filename, payload 누락·변조, checksum 불일치를
fail-closed 처리한다. collision payload를 바꿀 때 checksum 문자열만 직접 고치지 않는다.

Unreal `GroundCollisionExporter`는 ground와 static CSV를 모두 임시 파일에 완성한 뒤
각 payload를 교체하고, 두 파일의 실제 bytes checksum을 가진 `manifest.cfg`를 마지막에
commit한다. marker가 0개여도 strict header-only `static_colliders.csv`를 새로 써 stale
장애물을 제거한다. 저장이 중간에 끊기면 이전 manifest가 새 payload를 승인하지 않는다.

## `ground_surface.csv`

고정 header 뒤 각 행이 ENU meter 삼각형 하나다.

```csv
surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2
```

이 파일은 휠 ground hit와 차량 Z·pitch·roll을 위한 표면이다. provider는 package 범위와
분포에 맞춘 adaptive grid로 ray 후보를 줄이고, 겹침이 과도해 안전하게 index할 수 없는
입력에만 한정된 global fallback을 사용한다.

## `static_colliders.csv`

정적 벽·커브·barrier를 수평 OBB와 수직 interval로 나타낸다.

```csv
collider_id,semantic,shape,center_e_m,center_n_m,center_u_m,heading_rad,half_length_m,half_width_m,half_height_m,friction,restitution
```

- `semantic`: `wall`, `curb`, `barrier`
- `shape`: 현재 `obb`만 지원
- `heading_rad`: North=0, clockwise-positive navigation heading
- 세 half extent는 양수여야 하고 friction은 0 이상, restitution은 0~1이어야 한다.
- header-only 파일은 유효하며 authored blocker가 0개임을 뜻한다.

Unreal Editor에서는 `SimCore Static Collider` actor를 wall/curb/barrier마다 배치하고
고유 ASCII `Collider Id`, semantic, box 크기·Yaw, friction/restitution을 지정한다.
`GroundCollisionExporter`의 `Bake Ground + Static Collision To MapPackage`가 활성 marker를
자동 수집해 `map_enu` OBB 행으로 쓴다. marker의 Pitch/Roll, 빈·중복 ID와 잘못된 값은
bake를 실패시킨다.

C++는 manifest에 선언된 strict CSV만 로드하고 OBB-prism SAT, 최대 0.10m/1° microstep,
64-step fail-closed clamp, projection과 normal/friction impulse로 Ego의 ENU XY·yaw를
해결한다. 8m deterministic uniform-grid broad phase가 정적·동적 후보를 ID 순으로
선택하며, cell 예산을 넘는 큰 collider만 안전 fallback으로 처리한다. NPC kinematic OBB와
보행자 vertical capsule은 opt-in `--demo-entities`에서 lifecycle·collision snapshot·
`WorldState` 발행까지 연결돼 있고 Unreal은 이를 transient proxy로 표시한다. 이 demo
fixture는 LaneGraph·신호·R1 목표 개체 수를 갖춘 최종 traffic 기능을 뜻하지 않는다.

## 현재 범위와 후속 데이터

현재 tracked package는 collision contract의 최소 형식이다. 특히
`landscape_local_v1/static_colliders.csv`는 marker가 아직 bake되지 않은 header-only
파일이므로 정상 로드 수는 `static_colliders=0`이다. 코드 경로의 자동 구현과 별개로
marker 배치·재bake·서버 재시작 뒤 실제 PIE 벽/동적 충돌 확인이 WP-03 수동 gate로 남는다.
LaneGraph, georeference,
신호, 보행 경로, spawn, 원본·라이선스 metadata는 WP-05~07에서 manifest format을
버전 상승시켜 추가한다. `manifest.json`이나 임의 `collision.*` 파일은 현재 runtime
규약이 아니다.

2026-08-26 최종 자동 검증에서 Release CTest 11/11, 핵심 4종 각 20회와 UE 5.6 Editor
build가 통과했다. `landscape_local_v1` smoke는 checksum
`fnv1a64:239e5e39f3706396`, ground 2,220 triangles/1,872 cells, global fallback 0,
최대 후보 8, `static_colliders=0`, demo `runtime_count=2`, WebSocket `:9000` 시작을
확인했다. 이 결과는 실제 marker 또는 PIE 충돌 성공 근거가 아니다.

대용량 원본 에셋을 추가하기 전에 라이선스와 Git LFS/외부 저장 정책을 먼저 결정한다.
