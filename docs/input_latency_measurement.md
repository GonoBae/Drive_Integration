# 패키지 입력→화면 최초 반응: 고속 영상 수동 측정

이 절차는 실제 패키지에서 **물리 키 움직임 시작부터 화면에 보이는 차량 반응까지**를
한 영상의 프레임으로 측정한다. 서버가 명령을 적용했다는 ACK의 왕복 시간이나
클라이언트 입력 콜백→서버 적용 지연과 같은 지표가 아니다. 키 이동 시간, USB 입력,
게임·서버 처리, 물리 반응이 보일 때까지의 시간, 렌더링, 디스플레이가 모두 포함된다.
각 조작 10회 이상에서 보수적 상한의 최댓값이 50ms 이하여야 이 수동 측정 게이트를 통과한다.
현재 문서는 실제 측정 결과를 제공하지 않는다. 자동 테스트의 임시 합성 자료는 실측 증거가 아니다.

## 촬영 준비와 범위

1. [Windows 배포 절차](windows_package.md)로 만든 패키지의 서버와 게임을 실행한다.
   Editor/PIE, NullRHI, 재생 영상, 키 자동 반복은 사용하지 않는다. 실행한 클라이언트·서버
   EXE의 실제 경로, `package_manifest.json`, SHA-256을 기록한다. 클라이언트의 경우
   부트스트랩 EXE가 아닌 실행 중 게임 프로세스의 EXE를 기록한다. 파일은 측정 후에도 보존한다.
2. 시험 PC CPU/GPU, OS, 키보드 모델·연결 방식, 디스플레이 주사율, 해상도, VSync·프레임 제한,
   게임 카메라 모드, 지도·차종, 서버 접속 위치를 기록한다. 조건을 바꾸면 새 측정 묶음을 만든다.
   게임을 충분히 예열하고 연결·제어권이 정상인지 확인한다.
3. 실제 **120fps 이상**을 촬영하는 카메라를 고정한다. 240fps 이상이면 판독 여유가 커진다.
   물리 키의 측면 이동과 화면의 반응 영역이 같은 영상에서 동시에 선명하게 보여야 한다.
   가능하면 두 영역을 영상의 비슷한 높이에 배치하여 롤링 셔터의 행간 차이를 줄인다.
   카메라와 게임 카메라를 조작 중 움직이지 않고 손으로 반응 영역을 가리지 않는다.
4. 원본의 연속 프레임과 실제 촬영 속도를 확인한다. 슬로모션 파일의 30fps **재생 속도**를
   촬영 fps로 쓰지 않는다. 예를 들어 240fps 촬영 원본이면 `capture_fps=240`이다.
   시간축이 변하는 영상, 가변 촬영 간격, 프레임 누락·복제·보간, 편집·재인코딩된 영상은
   이 계산의 근거가 될 수 없다. 촬영 모드 정보와 프레임 검토 결과를 함께 보존한다.
5. 각 조작의 측정 횟수와 판독 기준을 먼저 정한다. 최소 10회씩 모든 시도를 기록한다.
   각 입력을 완전히 놓고 기준 상태를 회복한 다음 몇 초 간격을 두어 한 번씩 누른다.
   지연이 길거나 무응답인 시도도 CSV에 남긴다. 촬영 실패를 다시 찍으면 기존 시도를
   숨기지 않고 별도 실패 기록으로 보존하며, 조건 미충족 묶음을 통과 자료로 제출하지 않는다.

## 네 조작과 최초 반응 정의

동일한 평지의 장애물 없는 장소와 세단으로 시작한다. 조작 중 다른 키를 누르거나
카메라·차종을 바꾸지 않는다. 다음 기준 또는 더 명확한 사전 정의를 `response_criteria`에 적는다.

| CSV action | 준비 및 한 번의 입력 | response_frame에 표시할 최초 화면 반응 |
|---|---|---|
| `accelerate` | 차체가 완전히 정지하고 브레이크·조향을 놓은 상태에서 `W` 누름 | 정지 상태와 비교해 차체가 고정된 지면 기준점에 대해 처음 전진한 프레임 |
| `brake` | 직진 속도를 올린 뒤 `W`를 놓고 자연 감속 기준을 관찰한 상태에서 `S`만 짧게 누름 | 직전 자연 감속 추세를 넘어 이동량이 감소하는 첫 프레임; 화면 차량 속도 표시를 보조로 검토 |
| `steer_left` | 저속 직진, 조향 중앙, 다른 키를 놓은 상태에서 `A`만 누름 | 앞바퀴 또는 차량 진행 방향이 왼쪽으로 처음 변화한 프레임 |
| `steer_right` | 저속 직진, 조향 중앙, 다른 키를 놓은 상태에서 `D`만 누름 | 앞바퀴 또는 차량 진행 방향이 오른쪽으로 처음 변화한 프레임 |

`S`는 전진 중 서비스 브레이크다. 정지까지 계속 눌러 후진으로 전환하는 구간은 측정하지 않는다.
`Space`는 사이드 브레이크이므로 위 `brake`와 바꾸지 않는다. 조향 표시로 바퀴를 택했다면
모든 해당 시도에 같은 기준을 쓴다. 키 입력을 즉시 표시하는 UI, 브레이크등, 입력 바 차트는
실제 차량 응답을 대신하지 않는다. 움직이는 추적 카메라에서는 고정 지면 기준점과 차량의
상대 변화를 보아야 하며, 카메라 보정만으로 생긴 이동을 응답으로 세지 않는다.

브레이크는 원래 자연 감속 중이므로 특히 판독이 어렵다. 입력 전 충분한 프레임에서
기준 이동량을 확인하고, 입력 후 여러 프레임에서 지속되는 변화인지 검토한 뒤 최초 변화
프레임을 표시한다. 가속의 픽셀 이동·속도계 반올림·물리 조향 지연도 최초 검출을 늦출 수 있다.
이 추가 시간 때문에 이 방법은 보수적인 종단간 측정이다. 50ms 초과는 이 수동 기준의 실패이며,
서버 ACK 지연이 50ms를 초과했다는 증거는 아니다. 자연 감속·기존 조향과 구분할 수 없거나
끝점을 한 프레임 이내로 정할 수 없으면 임의로 빠른 프레임을 고르지 말고 무응답/판독 불가로 남긴다.

## 프레임 판독과 CSV

원본을 프레임 단위로 이동할 수 있는 뷰어에서 첫 디코딩 프레임을 **0**으로 번호 매긴다.
`input_frame`은 손가락이 닿은 때가 아니라 **키 자체가 처음 내려가기 시작한 프레임**이다.
늦은 바닥 접촉 프레임을 고르면 전기적 입력이 이미 발생했을 수 있으므로 사용할 수 없다.
`response_frame`은 위에서 정의한 최초 반응 프레임이다. 양 끝점은 반드시 같은
`video_file`의 연속 촬영 프레임이어야 한다. 키 움직임이 가려져 입력 끝점을 정할 수 없으면
그 묶음의 `physical_key_and_screen_visible` 등을 false로 기록하고 통과 증거로 쓰지 않는다.

CSV는 다음 헤더와 마지막 줄바꿈을 가진다. 이 문서에는 실측처럼 보일 수 있는 예시 행을 넣지 않는다.

```csv
action,trial,video_file,input_frame,response_frame,capture_fps
```

`trial`은 action마다 1부터 시작하고 빠짐없이 증가한다. `video_file`은 JSON의 영상 `file`과
정확히 같은 문자열이며 CSV/JSON 폴더 기준 상대 경로나 절대 경로다. 프레임은 음이 아닌 정수,
fps는 유한한 양수다. 무응답·모호한 반응은 **response_frame 칸만 비운 채** 시도를 남긴다.
원본 영상은 시도 후 반응 관찰 구간까지 보존한다. 중복 입력 프레임, 겹치는 입력→반응 구간,
누락된 시도 번호, 예상 횟수 불일치, 잘못된 fps·프레임은 정상 자료로 처리하지 않는다.

계산은 다음과 같다. 각 끝점의 한 프레임 오차를 모두 더한다.

```text
관측 지연(ms) = (response_frame - input_frame) × 1000 / capture_fps
보수적 상한(ms) = (response_frame - input_frame + 2) × 1000 / capture_fps
```

120fps에서는 오차 여유만 약 16.67ms다. 따라서 관측 간격 4프레임의 상한이 정확히 50ms이며,
5프레임이면 실패다. 롤링 셔터, 노출·잔상, 판독 불확실성을 모두 고려해 **각 끝점이 한 프레임
이내**라는 조건을 충족해야 한다. 더 큰 오차나 불명확한 실제 촬영 속도는 이 공식으로 보장할 수 없다.

## JSON 증거와 실행

CSV 옆에 같은 이름의 `.json`을 만든다. 아래는 **빈 작성 양식**이며 유효한 실측 자료가 아니다.
해시·경로·횟수·조건은 실제 파일과 촬영 기록으로 채운다. 모든 조건을 일괄 true로 바꾸지 않는다.
원본 영상 여러 개면 `videos` 항목을 늘리고 각 파일의 촬영 fps와 프레임 수를 적는다.
해시는 PowerShell의 `Get-FileHash -Algorithm SHA256 -LiteralPath '실제 파일 경로'`로 확인한다.

```json
{
  "schema_version": 1,
  "metric": "physical_key_to_first_visible_vehicle_response_ms",
  "mode": "packaged",
  "evidence_kind": "manual_video",
  "operator": "",
  "captured_at": "",
  "test_conditions": "",
  "source_package_manifest": {"file": "", "sha256": ""},
  "package_files": {
    "client_executable": {"file": "", "sha256": ""},
    "server_executable": {"file": "", "sha256": ""}
  },
  "videos": [{"file": "", "sha256": "", "capture_fps": null, "frame_count": null}],
  "attempts_per_action": {"accelerate": 0, "brake": 0, "steer_left": 0, "steer_right": 0},
  "response_criteria": {"accelerate": "", "brake": "", "steer_left": "", "steer_right": ""},
  "manual_conditions": {
    "stationary_camera": false,
    "physical_key_and_screen_visible": false,
    "original_continuous_frames": false,
    "capture_rate_verified": false,
    "no_dropped_or_interpolated_frames": false,
    "isolated_inputs": false,
    "all_attempts_recorded": false,
    "first_physical_key_motion_used": false,
    "endpoint_uncertainty_at_most_one_frame": false
  }
}
```

`source_package_manifest.file`은 CSV 폴더 기준 경로다. `package_files.*.file`은 해당 manifest가
있는 **배포 루트 기준**이며 실제 패키지 안의 서로 다른 EXE를 지정한다. 도구는 manifest·EXE·영상의
실재 여부, 비어 있지 않음, 예상 SHA-256과 실제 파일 내용의 일치를 검사한다. 출력에는 CSV와 JSON
자체의 해시도 남긴다. 패키지 내용을 변경했으면 예전 촬영에 새 해시를 덮어쓰지 말고 다시 측정한다.
이 해시는 검토하는 파일을 식별할 뿐 해당 실행 파일로 촬영했다는 사실이나 영상 내용을 인증하지 않는다.
실행 중 프로세스 경로·화면 조건·프레임 번호의 적절성·누락 없는 시도 기록은 사람이 함께 검토한다.

패키지 manifest의 `format_version` **1과 2를 모두 지원**한다. v1은 기존 패키지 호환용으로,
manifest 자체와 측정 메타데이터의 실행 파일 해시만 확인하며 결과에
`package_inventory_cross_check=unavailable_in_manifest_v1`을 표시한다. v2는 추가로
manifest의 `files` 목록에 아래 두 실행 파일이 존재하고, `path`·`bytes`·`sha256`이
측정 메타데이터 및 실제 파일과 일치해야 한다.

- `client_executable`: `Windows/DriveIntegration/Binaries/Win64/DriveIntegration.exe`
- `server_executable`: `cpp/host/build/Release/simcore_publisher.exe`

v2에서 다른 EXE나 부트스트랩 EXE를 이 역할로 지정하면 거부한다. 중복·패키지 밖 경로,
누락된 inventory 항목, 파일 크기·해시 불일치도 거부한다. 성공 결과의
`package_inventory_cross_check=client_and_server_bytes_and_sha256_verified`는 이 두 실행 파일만의
교차 검증이다. 이 분석기는 전체 패키지 inventory나 `source_snapshot`의 소스 파일을 검증하는
도구가 아니며, 해당 출처 기록을 포함하는 manifest의 해시와 원문을 결과에 보존한다.

저장소 루트에서 실제 증거 경로를 넣어 실행한다.

```powershell
python .\scripts\analyze_input_latency.py '실제 증거 폴더\input_latency.csv' --require-measurement-gate
python -m unittest discover -s scripts -p test_analyze_input_latency.py -v
```

출력 JSON은 action별 시도·무응답 횟수, 최대 보수적 상한, 모든 개별 시도와 출처 해시를 포함한다.
옵션을 넣은 종료 코드 `0`은 이 수동 측정 조건과 50ms 계산 기준 충족, `1`은 기준 미달,
`2`는 잘못되거나 누락된 증거다. 옵션 없이 종료 `0`이면 분석이 끝났다는 뜻이며 통과를 뜻하지 않는다.
`measurement_gate_met`은 선언된 수동 조건에 따른 판정이므로 원본 검토와 함께 해석한다.
`synthetic_fixture` 자료는 계산이 맞아도 측정 게이트를 통과하지 않는다.

이 분석은 기존 FPS 분석기에 통합되어 있지 않으며 서버 applied-ACK 지연, 30분 FPS·state 간격,
30분 연속 주행·안정성, 전체 배포 인수를 완료 처리하지 않는다. 각각 별도 증거가 필요하다.
