# Autonomy server placeholder

R2 이후 자율주행 서버를 이 경로에 독립 패키지로 구현한다. 현재 8월 수동운전 릴리스에는 포함하지 않는다.

예정 책임은 route planning, behavior planning, trajectory generation, 센서 전처리, `ControlCommand` 생성이다. 차량 물리와 충돌 계산은 계속 C++ SimCore가 담당한다.
