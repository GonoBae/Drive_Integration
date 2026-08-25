# Python services(R1 동결)

Python은 R1 수동운전 기본 경로에서 실행하지 않는다. 현재 코드는 삭제하지 않고 향후
R2 자율주행 설계 때 재검토할 수 있도록 보존한다.

| 경로 | 역할 | 수동운전 필수 여부 |
|---|---|---|
| `relay_server/` | default-OFF C++ ZMQ의 legacy `EntityStatePacket` 관찰, JSON 디버그 중계, 선택적 DB 기록 | 아니요(동결) |
| `autonomy_server/` | 향후 경로 계획, 행동 계획, 궤적 생성, AI 추론 | R2 이후 |

자율주행 구현을 시작할 때 기존 relay에 AI 코드를 추가하지 않고 `autonomy_server/`를 별도 패키지로 생성한다. 두 프로세스는 루트 `protocol/vehicle.proto`를 공유한다.

Relay의 FastAPI lifespan은 ZMQ subscriber와 선택적 DB를 시작하고, 종료 시
subscriber task를 cancel/await한 뒤 DB pool을 닫는다. `/` health 응답에서
subscriber 실행·수신·오류 상태를 확인할 수 있다.

기본 C++ host는 `SIMCORE_ENABLE_ZMQ_OBSERVER=OFF`라 ZeroMQ를 컴파일하거나 5555
포트를 열지 않는다. opt-in observer는 version field가 없는 legacy packet을 사용하므로
schema-v2 R1 계약이나 자율주행 transport로 간주하지 않는다.

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

서버는 `.env`의 `HOST`, `PORT`를 사용한다. 위 실행 명령과 Python 시험은 legacy
observer를 명시적으로 점검할 때만 사용하며 R1 완료 조건에는 포함하지 않는다. Python
자율주행은 R2에서 version handshake와 control/state/sensor transport를 결정한 뒤
개발을 재개한다.
