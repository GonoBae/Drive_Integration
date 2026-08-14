# Python services

Python 프로세스는 역할별로 분리한다.

| 경로 | 역할 | 수동운전 필수 여부 |
|---|---|---|
| `relay_server/` | C++ ZMQ 상태 관찰, JSON 디버그 중계, 선택적 DB 기록 | 아니요 |
| `autonomy_server/` | 향후 경로 계획, 행동 계획, 궤적 생성, AI 추론 | R2 이후 |

자율주행 구현을 시작할 때 기존 relay에 AI 코드를 추가하지 않고 `autonomy_server/`를 별도 패키지로 생성한다. 두 프로세스는 루트 `protocol/vehicle.proto`를 공유한다.
