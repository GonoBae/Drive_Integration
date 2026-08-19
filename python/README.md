# Python services

Python 프로세스는 역할별로 분리한다.

| 경로 | 역할 | 수동운전 필수 여부 |
|---|---|---|
| `relay_server/` | C++ ZMQ 상태 관찰, JSON 디버그 중계, 선택적 DB 기록 | 아니요 |
| `autonomy_server/` | 향후 경로 계획, 행동 계획, 궤적 생성, AI 추론 | R2 이후 |

자율주행 구현을 시작할 때 기존 relay에 AI 코드를 추가하지 않고 `autonomy_server/`를 별도 패키지로 생성한다. 두 프로세스는 루트 `protocol/vehicle.proto`를 공유한다.

Relay의 FastAPI lifespan은 ZMQ subscriber와 선택적 DB를 시작하고, 종료 시
subscriber task를 cancel/await한 뒤 DB pool을 닫는다. `/` health 응답에서
subscriber 실행·수신·오류 상태를 확인할 수 있다.

## Relay 실행

모든 명령은 저장소 루트에서 실행한다. Relay는 패키지 상대 import를 사용하므로 서비스 내부 import가 현재 작업 디렉터리에 의존하지 않는다.

macOS/Linux:

```bash
python3 -m venv python/relay_server/.venv
python/relay_server/.venv/bin/python -m pip install -r python/relay_server/requirements.txt
python/relay_server/.venv/bin/python -m python.relay_server
```

Windows PowerShell:

```powershell
py -3.10 -m venv python\relay_server\.venv
python\relay_server\.venv\Scripts\python.exe -m pip install -r python\relay_server\requirements.txt
python\relay_server\.venv\Scripts\python.exe -m python.relay_server
```

서버는 `.env`의 `HOST`, `PORT`를 사용한다. Python 회귀 시험은 `make test-python`, Proto 동기화 검사는 `make check-proto`로 실행한다.
