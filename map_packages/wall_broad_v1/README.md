# Wall/Broad MapPackage v1

Wall Street·Broad Street 주변 수동운전 버티컬 슬라이스용 패키지 자리다.

현재 C++ bootstrap 기준원점은 Wall Street·Broad Street 교차점의 근사 위치인 WGS84 `40.70694, -74.01083`이다. 이는 최종 차량 스폰 지점이 아니다. 실제 원점의 ellipsoidal height와 차량 스폰 위치·방향은 지도 원본과 통행 가능한 차선을 검증한 뒤 `georeference.json`, `spawn_points.json`으로 확정한다.

예정 산출물:

- `manifest.json`
- `georeference.json`
- `lane_graph.*`
- `collision.*`
- `signals.*`
- `pedestrian_paths.*`
- `spawn_points.json`
