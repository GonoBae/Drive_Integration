# 외부 지도·배경 에셋 등록표

2026-08-31 생성. **현재 새로 취득·승인된 외부 배경 에셋은 없다.**
UE template/Engine content를 외부 에셋 신규 취득으로 기록하지 않는다.

에셋을 import하기 전에 아래 정보를 항목별로 기록한다. 아직 검토하지 않은 값은
`미확인`으로 두며, 라이선스 확인 전 공개 저장소에 원본을 push하지 않는다.

| ID | 이름·버전 | 원본 URL·제공자 | 취득일 | 라이선스·증빙 | 프로젝트 경로 | 공개 원본 배포 검토 | attribution·수정 이력 | UE 5.6 cook/성능 확인 |
|---|---|---|---|---|---|---|---|---|
| (취득 시 추가) | | | | | | | | |

증빙에 개인 결제정보·계정 토큰을 포함하지 않는다. 공개 문서에는 출처와 허용 범위만
남기고 영수증 등 개인정보는 별도 비공개 위치로 관리한다.

배경 범위와 작업 순서는 [배경 제작 계획](../04_environment_plan.md)을 따른다.

## 자체 제작 blockout — 2026-08-31

`/Game/VirtualCity`는 Engine `/Engine/BasicShapes/Cube.Cube`를 참조하는 자체 배치다.
Engine mesh 원본을 프로젝트 Content에 복사하지 않았다. 배치 원본은
`SimCoreVirtualCityLayout.cpp`, 저장형 단색 material 10개는
`/Game/VirtualCity/Materials/M_VC_*`에 생성했다. 외부 도시 팩·사진·텍스처를 취득하거나
import하지 않았다. 건물은 자체 기본 도형 외관이며 실제 건축물 재현이 아니다.

Editor/Game compile·저장 맵 재로드 검증은 통과했다. cook·패키징·목표 PC 성능 인수는
아직 남아 있으며, 나중에 외부 에셋으로 교체하면 위 등록표에 별도로 추가한다.

## 자체 제작 일반 세단 — 2026-08-31

`/Game/Vehicles/Sedan/SM_SedanBody`와 `SM_SedanWheel`은 이 저장소의
`BuildSedanVisualCommandlet.cpp`에서 형상 데이터를 생성한 StaticMesh다.
`/Game/Vehicles/Sedan/Materials/M_Sedan_*` 10종은 자체 상수/물성 material이다.
곡면 차체·창문·그릴·등화류·미러, 휠 아치, 원형 고무 타이어와 합금 휠을 포함한다.
외부 차량 팩/스캔/사진/bitmap texture를 취득하지 않았고 유료 구매도 하지 않았다.
특정 브랜드·실차 정밀 복제품이나 포토리얼 에셋으로 간주하지 않는다.

Editor/Game compile, geometry Automation, 저장 에셋 재로드 검증과 실제 렌더를 수행했다.
첫 렌더에서 발견한 램프/곡면 교차와 범퍼 이음새는 surface-conforming geometry로 보완했다.
시각 mesh collision은 꺼져 있으며 물리 형상·좌표·wheel pivot은 기존 계약을 유지한다.
cook/패키징, 최종 1080p 성능, 실내·기능성 등화류는 미검증/미구현이다.
수동 편집 모델은 generator로 덮어쓰지 않는다.
[제작·재현·검증 안내](../vehicle_driving_refinement.md).
