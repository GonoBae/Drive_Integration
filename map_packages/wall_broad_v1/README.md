# Wall/Broad MapPackage v1

Wall Street·Broad Street 주변 수동운전 버티컬 슬라이스의 bootstrap package다.

현재 C++ 기준원점은 교차점 근사 WGS84 `40.70694, -74.01083`이다. 최종 차량 스폰
지점은 아니며 ellipsoidal height, 정확한 ENU 원점, 주행 루프와 스폰은 지도 원본과
통행 가능 차선을 검증한 뒤 확정한다.

## 현재 runtime 파일

- `manifest.cfg`: format 1, `map_enu`, 아래 두 collision payload와 실제 FNV-1a 64
  checksum을 선언한다.
- `ground_surface.csv`: `surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2` 형식의 bootstrap ENU
  ground triangle이다.
- `static_colliders.csv`: `collider_id,semantic,shape,center_e_m,center_n_m,center_u_m,heading_rad,half_length_m,half_width_m,half_height_m,friction,restitution`
  strict schema다. 현재는 header-only이므로 authored wall/curb/barrier가 0개다.

현재 runtime은 `manifest.json`이나 임의 `collision.*`을 읽지 않는다. collision payload를
직접 수정하면 `manifest.cfg`의 checksum도 실제 파일 bytes 기준으로 다시 생성해야 하며,
불일치는 서버와 Unreal에서 시작을 차단한다.

C++ 정적 collision core는 OBB-prism SAT와 수직 interval, 0.10m/1° microstep,
64-step fail-closed clamp, projection과 normal/restitution/friction impulse를 지원한다.
adaptive ground grid와 8m deterministic collision broad phase, Unreal
`ASimCoreStaticCollider` authoring·원자적 export도 구현됐다. 그러나 이 package에는 아직
실제 Wall/Broad 벽·커브 OBB가 없고 marker bake·실제 PIE 충돌 확인이 남아 있으므로
WP-03 전체 완료 상태가 아니다.

## 후속 package 데이터

- `georeference`와 정확한 ENU 원점
- 방향성 LaneGraph와 통행 규칙
- 실제 주행면·커브·벽 static OBB
- 신호, 횡단보도, 보행 경로
- NPC·보행자·Ego spawn
- 원본 버전, 취득일, 라이선스와 변환 이력

이 데이터는 현재 format 1에 임의 key를 추가하지 않고 schema와 loader를 함께 버전
상승시켜 도입한다.
