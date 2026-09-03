# UE 5.6 편집 뷰포트 지면 떨림: 시간축 AA 진단과 우회

기록일: 2026-08-31. 대상은 `L_VirtualCity`의 **Play가 아닌 편집 뷰포트**다.
사용자가 `ShowFlag.TemporalAA 0` 적용 후 “안떨린다”고 확인했다. 이는 확인된
우회 결과이며, 엔진 버그를 확정하거나 영구 수정·장기 안정성 검증을 완료한 기록은 아니다.

## 관찰과 진단 결과

사용자 GIF는 1068×574, 188프레임, 총 6.31초였다. 영상에서는 도로·지면과
일부 편집 오버레이의 시간적 변화가 보였다. 도형 경계는 대체로 같은 위치에 있었지만,
GIF의 팔레트 양자화·디더링도 미세한 픽셀 변화에 영향을 줄 수 있으므로 영상만으로
렌더러의 특정 단계를 원인으로 확정하지 않았다.

| 비교/명령 | 실제 사용자 관찰 | 해석 범위 |
|---|---|---|
| Lit | 지면이 떨림 | 최초 증상 |
| Unlit | 떨리지 않음 | 조명뿐 아니라 후처리·AA 경로도 달라지므로 조명 원인으로 단정 불가 |
| `r.Lumen.DiffuseIndirect.Allow 0` | 해결되지 않음 | Lumen diffuse만 끄는 것은 유효한 우회가 아니었음 |
| `ShowFlag.DynamicShadows 0` | 해결되지 않음 | 동적 그림자만 끄는 것은 유효한 우회가 아니었음 |
| `ShowFlag.Grid 0` | 해결되지 않음 | 편집 그리드만 제거해서는 해결되지 않았음 |
| `ShowFlag.TemporalAA 0` | “안떨린다” 확인 | 시간축 AA를 제거한 편집 화면에서 증상이 멈춤 |

영상만으로 추정하지 않고 사용자 응답과 콘솔 로그를 구분해 확인했다. 효과가 없었던
Lumen diffuse는 `1`, 동적 그림자·그리드 show-flag 강제값은 `2`로 복원한 이력이 있다.
`ShowFlag.PostProcessing 0`은 이 결과를 확인한 실제 사용자 시험으로 기록하지 않는다.

별도 에디터 프로세스에서도 같은 맵을 대상으로 다음 네 조건을 각각 24장씩,
총 96장의 일반 뷰포트 PNG로 비교했다.

- TSR + selection outline 표시
- FXAA + selection outline 표시
- TSR + selection outline 숨김
- FXAA + selection outline 숨김

FXAA 조건에서 측정한 시간적 픽셀 변동이 줄었다. 그러나 카메라·선택 상태·뷰포트와
캡처 타이밍을 사용자의 화면과 완전히 일치시킨 재현은 아니다. 96장의 짧은 비교는
방향을 확인한 보조 증거이며, 사용자의 증상을 완전 재현했거나 장기 편집 안정성을
입증한 시험으로 세지 않는다. 로컬 진단 산출물은 ignored `runtime_logs`의
`editor-flicker-probe-20260831-155227/`와 해당 비교 스크립트에 있으며 배포 자료가 아니다.

현재 결론은 **이 화면에서 시간축 AA를 끄는 조작이 증상과 관련되어 있고, 실제 사용자에게
우회 효과가 있었다**는 수준이다. TSR 자체 결함, 특정 depth/overlay 상호작용, 겹친
도로 면 등 어느 하나를 확정 원인으로 부르지 않는다. 반대로 지오메트리는 원인이
아니라고 단정하지도 않는다.

## 편집 뷰포트에만 적용하는 권장 우회

아래는 적용 방법이다. 현재 확인된 사용자 조작은 콘솔의 `ShowFlag.TemporalAA 0`까지이며,
이 UI 설정으로 전환·저장·재시작 확인을 끝냈다는 증거는 아직 없다.

1. Play/Simulate를 끝내고 일반 편집 뷰포트로 돌아온다.
2. 콘솔에서 `ShowFlag.TemporalAA 2`를 입력해 현재 프로세스의 강제 override를 해제한다.
   `2`는 FXAA/TSR 선택값이 아니라 해당 show flag를 뷰포트 설정에 맡기는 값이다.
   강제값이 남아 있으면 해당 메뉴가 비활성화될 수 있다.
3. 해당 뷰포트의 **Show → Advanced → Temporal AA (instead FXAA)** 체크를 해제한다.
4. 일반 **Anti-aliasing** 항목은 켜 둔다. 이것까지 끄면 FXAA도 꺼진다.
5. 동일 위치에서 증상을 확인하고, 정상적인 에디터 설정 저장/종료 뒤 다시 열어 해당
   뷰포트의 체크 상태와 화면을 확인한다. 레이아웃의 다른 뷰포트는 별도 설정일 수 있다.

Epic의 UE 5.6 문서도 Advanced의 이 항목을 해제하면 뷰포트가 FXAA를 사용한다고
설명한다. [Viewport Show Flags — Advanced](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-show-flags-in-unreal-engine?application_version=5.6#advanced)

FXAA는 이전 프레임을 누적하지 않는 공간 기반 AA다. 시간축 AA/업스케일링과 화질 특성이
다르므로 이 우회 이후에도 얇은 선·먼 거리 도형의 계단 현상과 선명도는 별도로 확인한다.
[Anti-Aliasing and Upscaling — FXAA](https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine?application_version=5.6#fastapproximateanti-aliasing)

## 유지 범위와 변경하지 않은 것

- 위 UI 조작은 해당 편집 뷰포트의 show flags를 바꾼다. 저장 시
  `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`의
  `PerInstanceSettings → EditorShowFlagsString`에 반영되는 사용자·프로젝트별 설정이다.
  새 레이아웃/다른 뷰포트/다른 사용자에게 일괄 적용되는 제품 기본값이 아니다.
- 진단 콘솔의 `ShowFlag.TemporalAA 0`은 프로세스의 강제값이므로 그대로 둔 상태를
  “편집 화면만 FXAA, PIE는 TSR”로 해석하면 안 된다. 먼저 `2`로 해제한 뒤 편집 UI에서만
  설정해야 한다. Play 중 메뉴를 조작하면 PIE 게임 뷰포트에 적용될 수 있다.
- 이 우회를 위해 전역 `r.AntiAliasingMethod`나 Project Settings의 AA 방식을 FXAA로
  바꾸지 않는다. 편집 UI에만 적용하면 실제 게임의 기존 TSR 선택은 유지할 수 있다.
- 이 진단/우회 때문에 원본 맵, 도로 지오메트리, Bake 산출물, 차량 물리 또는 제품
  `DefaultEngine.ini`를 수정하지 않았다. 별도 진단 프로세스의 임시 설정과 구분한다.
- UI 적용 완료, 재시작 지속, 여러 시점/뷰포트 비교, 실제 게임 화질 및 장기 에디터
  안정성 시험은 미확인이다. 이 기록을 배경·물리 수정 완료나 안정성 게이트 통과로 세지 않는다.

## 설치된 UE 5.6 소스로 확인한 동작

공식 문서의 일반 설명을 다음 설치 소스와 대조했다. 엔진 파일은 수정하지 않았다.

- `Engine/Source/Runtime/Engine/Private/ShowFlags.cpp`, `ApplyViewMode`: Unlit은
  `PostProcessing=false`도 설정한다. 따라서 Lit/Unlit 비교만으로 조명 문제를 확정할 수 없다.
- `Engine/Source/Runtime/Engine/Private/SceneView.cpp`: `TemporalAA=false`이면 TSR/TAA를
  FXAA로 대체하고, `AntiAliasing=false`이면 AA 자체를 `AAM_None`으로 바꾼다.
- `Engine/Source/Editor/CommonMenuExtensions/Private/ShowFlagMenuCommands.cpp`와
  `ShowFlags.cpp::IsForceFlagSet`: 콘솔 강제값이 없는 경우에 UI show-flag 변경을 허용한다.
- `Engine/Source/Editor/LevelEditor/Private/SLevelViewport.cpp::SaveConfig`와
  `LevelEditorViewportSettings.h`: 편집 show flags를 뷰포트 인스턴스별
  `EditorShowFlagsString`으로 저장하며 설정 클래스는 `EditorPerProjectUserSettings`를 사용한다.

참고로 editor primitives와 selection outline은 별도 후처리 패스다. 또한
`CompositeEditorPrimitives=0`은 일부 편집 요소를 base pass로 옮기므로
“모든 편집 오버레이를 숨기는 단일 토글”로 사용하지 않는다. 이 경로 차이는 이번 원인을
확정하는 증거가 아니라 후속 비교에서 변수를 잘못 묶지 않기 위한 주의사항이다.
