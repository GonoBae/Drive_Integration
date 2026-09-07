# 에셋 출처·제작 기록

2026-08-31 생성, 2026-09-07 갱신. 현재 외부 도시·차량 팩은 사용하지 않습니다.
도시와 차량은 저장소 코드로 만든 에셋이며, 사람은 UE 5.6에 포함된 Epic의 Manny/Quinn을
사용합니다. 자체 제작물과 엔진에서 가져온 원본을 아래에 구분해 기록합니다.

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
cook/패키징과 최종 1080p 성능 인수는 남아 있다. 실내·유리·문·기능성 등화류는
9/4 후속에서 구현했으며 아래에 현재 상태를 기록한다.
수동 편집 모델은 generator로 덮어쓰지 않는다.
[제작·재현·검증 안내](../vehicle_driving_refinement.md).

## 현재 차량 에셋 — 2026-09-04~05

제작 원본은
[`BuildSedanVisualCommandlet.cpp`](../../unreal/DriveIntegration/Source/DriveIntegrationEditor/BuildSedanVisualCommandlet.cpp)입니다.
이 코드에서 형상·재질을 생성해 프로젝트 Content에 저장합니다. 외부 차량 모델이나
사진·스캔 텍스처를 가져온 것은 아닙니다.

| 에셋 | 프로젝트 경로 | 제작·사용 방식 |
|---|---|---|
| 세단 차체·바퀴 | `/Game/Vehicles/Sedan/SM_SedanBody`, `SM_SedanWheel` | 자체 형상. 차체 창문과 운전석 문에 실제 개구부를 만들었습니다. |
| 운전석 문 | `/Game/Vehicles/Sedan/SM_SedanDoorLeft` | 차체에서 외판·유리·손잡이·미러를 분리해 생성합니다. `-UpdateDriverDoor`로 갱신하며 런타임은 표시용 힌지를 회전합니다. |
| 유리·등화 재질 | `/Game/Vehicles/Sedan/Materials/M_Sedan_*` | 기본 10종에 `M_Sedan_TurnIndicator`를 추가했습니다. `-UpdateGlass`는 양면·24% 불투명도 유리와 개구부를 갱신합니다. 방향지시등은 발광 재질과 런타임 조명을 함께 사용합니다. |
| 경차 | `/Game/Vehicles/NpcFleet/SM_CompactBody` | 자체 세단 형상·문을 합치고 축소해 생성합니다. |
| 트럭 | `/Game/Vehicles/NpcFleet/SM_TruckBody` | 자체 상자·곡면 형상으로 운전실·적재함·등화류를 구성합니다. |
| 오토바이 | `/Game/Vehicles/NpcFleet/SM_MotorcycleBody` | 자체 튜브·곡면으로 프레임·탱크·안장·포크·등화류를 구성합니다. |

세 가지 NpcFleet 모델은 `-UpdateNpcFleet`로 생성하며 세단 재질과 바퀴를 재사용합니다.
NPC뿐 아니라 9/5부터 플레이어 차종 선택에도 사용합니다. 현재는 기능 확인용 모델이며
특정 실차를 정밀하게 재현한 자산은 아닙니다. 세단 외 차종에는 세단용 실내·운전자를
표시하지 않습니다.

세단 실내·운전자의 배치와 문 여닫기·손짓은 런타임 표시 코드가 담당합니다. 운전자 외형의
원본은 아래 Epic 캐릭터이며, 하차·손짓·착석 포즈는 프로젝트의 절차적 포즈·IK입니다.
문은 Chaos 관절 문이 아니며 차체 찌그러짐도 저장 원본을 바꾸지 않는 런타임 정점 변형입니다.

## Epic 기본 캐릭터 — 2026-09-03 반입

| 항목 | 기록 |
|---|---|
| 제공자·이름 | Epic Games, UE 5.6 Manny/Quinn mannequin |
| 설치 원본 | `B:/Epic Games/UE_5.6/Templates/TemplateResources/High/Characters/Content/Mannequins` |
| 프로젝트 경로 | `/Game/Characters/Mannequins` |
| 반입 범위 | 25개 `.uasset`, 약 80MB. 선택 메시·공유 스켈레톤·rig/PhysicsAsset·재질/텍스처·idle/forward walk 의존성 |
| 원본 변경 | 출처 기록 기준 원본 파일을 그대로 복사했습니다. template 맵·게임플레이 코드는 반입하지 않았습니다. |
| 사용 | 보행자와 세단 운전자 표시. 보행자 부분 래그돌은 UE에서 표시하며 최종 충돌·몸통 상태는 서버가 소유합니다. |
| 공개 원본 배포 검토 | 9/7에 아래 설치 원본과 UE EULA의 Examples 정의·5(b) 배포 조항을 확인했습니다. 이 Templates 경로에서 복사한 파일을 Examples로 분류해 프로젝트에 포함합니다. Epic 저작권과 적용 약관은 유지합니다. |

반입 당시 기록은
[`ASSET_PROVENANCE.md`](../../unreal/DriveIntegration/Content/Characters/Mannequins/ASSET_PROVENANCE.md)에
있습니다. 자체 제작 캐릭터로 소개하지 않습니다.

확인한 [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal)는 설치 디렉터리의
Samples·Templates 콘텐츠를 Examples로 정의하고, 5(b)에서 소스·오브젝트 형태 배포를
허용합니다. 이 판단은 위 템플릿 출처에 한정하며 Fab 구매 에셋이나 다른 엔진 콘텐츠의
원본 배포까지 허용한다는 뜻은 아닙니다.

9/5까지 Editor 빌드·자동 검사는 통과했으나, 최신 차종의 비율·문과 손발 동작·충돌 후
표시 자연스러움은 최종 플레이 확인이 남아 있습니다. 자동 검사 통과와 배포·성능 인수는
별도로 관리합니다. [9/4 작업 기록](../worklogs/2026-09-04.md),
[9/5 작업 기록](../worklogs/2026-09-05.md).
