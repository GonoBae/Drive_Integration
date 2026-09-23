# 개발 검증 도구

현재 실행 경로는 UE 5.6과 C++ 서버다. 이 폴더의 Python은 프로토콜 정합성,
서버 통신, 성능·입력 지연 검증에만 사용하며 배포 앱 실행에는 필요하지 않다.

## 파일 구분

| 용도 | 파일 |
|---|---|
| 서버 실행 | `run_signal_city_server.ps1`(기본 맵), `run_virtual_city_server.ps1`, `run_landscape_server.ps1` |
| 실행 공통 처리 | `server_launcher_common.ps1` — 포트·파일 확인, 백그라운드 실행 |
| 배포 | `package_windows.ps1`, `smoke_windows_package.ps1` |
| 통신 코드 생성 | `generate_proto.ps1`, `generated/`, `check_generated_proto.py` |
| 통합 검사 | `check_core.py`, `smoke_*.py` |
| 측정 결과 분석 | `analyze_performance.py`, `analyze_input_latency.py` |
| 위 도구의 회귀 시험 | `test_*.py`, `test_*.ps1` |

파일은 실행용·검사용 역할로 구분하되 기존 경로를 유지한다. 도구끼리 같은 폴더의 모듈을
불러오고 배포 스크립트도 이 위치를 사용하므로 개별 파일을 임의로 옮기지 않는다.

## 설치와 검사

Windows PowerShell에서 저장소 루트를 기준으로 실행한다. Python 3.10 이상과
`scripts/requirements.txt`의 의존성을 사용한다.

```powershell
python -m pip install -r scripts/requirements.txt
python scripts/check_generated_proto.py -v
python -m unittest discover -s scripts -p 'test_*.py' -v
python scripts/smoke_signal_city.py --preflight-only
```

`protocol/vehicle.proto`를 수정하면 바인딩을 갱신하고 일치 검사를 다시 실행한다.
생성 파일은 직접 수정하지 않는다. 활성 Python 환경 대신 특정 실행 파일을 쓰려면
생성 스크립트에 `-Python '경로/python.exe'`를 전달한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/generate_proto.ps1
python scripts/check_generated_proto.py -v
```

Core 전체 자동 검증은 [검증 안내](../docs/guides/core_validation.md)를 따른다.
이미 빌드한 C++ 서버로 준비물만 점검하려면 아래 명령을 사용한다.

```powershell
python scripts/check_core.py --skip-build --skip-unreal --preflight-only
```

생성 파일 검사는 별도 단계이므로 `test_*.py` 자동 발견에는 중복 포함하지 않는다.
Preflight는 테스트 통과나 실제 운전·패키지 성능 검증을 의미하지 않는다.
