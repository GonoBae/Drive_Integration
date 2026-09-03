# 질문 및 답변

프로젝트를 공부하며 질문한 내용을 빠르게 다시 찾는 문서다. 빠른 찾기에는 **질문과 한 줄 답변만** 표시한다. Quiz 정답은 별도 구간에서 제공한다.

## 빠른 찾기

| ID | 질문 | 한 줄 답변 |
|---|---|---|
| S01-QA-001 | proto3란 무엇인가? | 여러 언어가 공유할 binary 데이터 구조와 호환 규칙을 선언하는 Protobuf 스키마 문법이다. |
| S01-QA-002 | `message`는 C++ 구조체인가? | 비슷한 역할은 하지만 언어 중립 선언이며 C++에서는 직렬화 기능을 가진 class가 생성된다. |
| S01-QA-003 | 필드 뒤 숫자는 왜 필요한가? | 실제 값이 아니라 binary에서 필드를 식별하는 고정 field number다. |
| S01-QA-004 | `wheel_index = 1`과 주석의 0~3은 왜 다른가? | `1`은 필드 번호이고 0~3은 실행 중 그 필드에 담기는 바퀴 위치 값이다. |
| S01-QA-005 | `repeated`는 무엇인가? | 같은 타입을 0개 이상 담는 목록이며 C++의 `vector`와 개념적으로 비슷하다. |
| S01-QA-006 | 각 Proto message는 무슨 역할인가? | 입력, 차량·휠 상태, 진단과 metadata를 역할별 message로 나눈다. |
| S01-QA-007 | `Hello`와 `Health`도 Envelope에 들어가는가? | 들어갈 수 있지만 한 Envelope에는 `oneof`가 선택한 payload 하나만 담긴다. |
| S01-QA-008 | 정의만 있는 기능은 나중에 공부해도 되는가? | 지금은 역할과 미구현 상태만 알고, 실제 흐름과 시험은 구현될 때 깊게 공부한다. |

## Quiz 정답 및 해설

각 문제 아래의 `정답과 해설 보기`를 누르면 정답을 확인할 수 있다. 정답은 항상 접힌 상태로 둔다.

### S01-P1 Quiz

#### P1-Q1. 타입 이름 변경과 wire 호환성

> `VehicleGear` 이름을 `Gear`로 바꾸되 field number와 enum number를 유지하면 wire byte를 읽는 데 반드시 실패할까?

<details>
<summary>정답과 해설 보기</summary>

반드시 실패하지는 않는다. 일반 Protobuf binary에는 enum 타입 이름이 들어가지 않으므로 관련 field number와 enum 숫자가 같으면 기존 수신자는 wire 데이터를 읽을 수 있다. 하지만 생성 C++ 타입 이름이 바뀌므로 기존 `simcore::VehicleGear`를 참조하는 소스는 수정·재생성 전까지 컴파일되지 않는다. 즉, **wire 호환성과 source/API 호환성은 별개**다.

</details>

#### P1-Q2. 공개된 enum 번호 변경

> `VEHICLE_GEAR_DRIVE`의 번호를 1에서 2로 바꾸면 기존 기록 데이터에 어떤 문제가 생길까?

<details>
<summary>정답과 해설 보기</summary>

기존 Drive 데이터는 숫자 1로 저장돼 있으므로 새 코드는 이를 Drive로 해석하지 못하거나 다른 의미로 처리할 수 있다. 현재 숫자 2는 이미 Reverse이므로 그대로 바꾸면 중복 enum 번호 때문에 schema 생성부터 실패한다. 별칭을 허용하더라도 Drive와 Reverse를 wire에서 구분할 수 없다. 공개된 enum 번호는 바꾸지 않고 새 의미에 새 번호를 사용해야 한다.

</details>

#### P1-Q3. 누락된 ControlMode

> `ControlMode`가 누락된 command는 현재 생성 코드에서 어떤 mode로 보일 가능성이 큰가?

<details>
<summary>정답과 해설 보기</summary>

일반 proto3 enum field이므로 숫자 0인 `CONTROL_MODE_MANUAL`로 보인다. 일반 accessor만으로는 “필드 누락”과 “Manual 0을 명시적으로 보냄”을 구분하기 어렵다. 안전상 구분이 필요하면 `UNSPECIFIED=0`, `optional`과 명시적 검증 같은 설계를 검토해야 한다.

</details>

#### P1-Q4. `Vector3d.x = 1`의 숫자

> `Vector3d.x = 1`에서 1은 x의 기본값인가?

<details>
<summary>정답과 해설 보기</summary>

아니다. 1은 binary에서 `x`를 식별하는 field number다. 실제 x 값은 실행 중 별도로 설정하며, 필드가 누락되면 proto3 `double`의 기본값인 `0.0`으로 보인다.

</details>

#### P1-Q5. 좌표에 double을 쓰는 이유

> 위도·경도나 큰 ENU 좌표에 `double`을 쓰는 이유를 `float`의 정밀도와 연결해 설명할 수 있는가?

<details>
<summary>정답과 해설 보기</summary>

`float`는 유효 정밀도가 약 7자리라 위도·경도나 값이 큰 좌표에서 작은 위치 변화가 반올림으로 사라질 수 있다. `double`은 약 15~16자리 정밀도를 제공해 좌표 변환과 누적 계산 뒤에도 미터 이하 변화를 더 안정적으로 유지한다. 다만 정밀도 문제는 `double`만으로 끝나지 않으므로 로컬 원점·좌표계·단위 계약도 함께 관리해야 한다.

</details>

## 핵심 연결 관계

### 입력과 상태의 왕복

```text
Unreal 입력
→ Envelope{ControlCommand}
→ C++ protocol adapter
→ VehiclePhysics 계산
→ C++ protocol adapter
→ Envelope{WorldState}
→ Unreal 표시
```

`VehiclePhysics`가 Protobuf를 직접 처리하는 것은 아니다. protocol adapter가 `ControlCommand↔VehicleInput`, `VehicleState↔WorldState`를 변환한다. Python relay는 수동운전 왕복에 포함되지 않으며, default-OFF ZMQ를 명시적으로 켠 경우에만 버전 없는 구 `EntityStatePacket`을 별도로 읽는다.

### Envelope payload

```text
연결 협상: Envelope{Hello}           # 현재 양방향 runtime 구현
운전 입력: Envelope{ControlCommand}  # 현재 구현
물리 상태: Envelope{WorldState}      # 현재 구현
상태 진단: Envelope{Health}          # 목표, 현재 runtime 미구현
```

한 Envelope에 네 message를 모두 넣는 것이 아니라, 필요할 때 payload 하나를 선택해 별도 frame으로 보낸다.

## 기록 규칙

- 프로젝트 이해에 필요한 일반 개념 질문도 기록한다.
- 답변 전문은 복사하지 않고 핵심 결론과 프로젝트 적용을 요약한다.
- 질문마다 `Sxx-QA-nnn` ID를 부여한다.
- Quiz는 `Sxx-Pn-Qn` ID를 사용하고 문제마다 정답·해설을 항상 접어서 제공한다.
- 잘못 이해한 내용이 확인되면 한 줄 답변을 최신 내용으로 바로잡는다.
- 코드·설계·일정에 영향이 있으면 관련 문서와 작업 ID도 함께 갱신한다.

[학습 센터로 돌아가기](./README.md) · [학습자료 열기](./lessons/README.md)
