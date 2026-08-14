# Chrono::Vehicle D1 스파이크

이 프로그램은 Chrono::Vehicle의 서버형 사용 가능성을 검증하기 위한 헤드리스 벤치마크다. 하나의 물리 월드에 Sedan 1대 또는 여러 대를 만들고 TMeasy 타이어, SMC 접촉, 고정 timestep, rigid terrain을 사용한다. 같은 입력을 세 번 실행하여 처리 속도와 최종 상태 반복성을 출력한다. 실행 결과에는 실제로 링크된 Chrono 버전이 포함된다.

비교 기준 버전은 **Chrono 10.0.0**이다. 소스는 비교 결과를 재현할 수 있도록 Chrono 8과 10 API를 모두 수용한다. 현재 제품은 Chrono를 런타임 의존성으로 사용하지 않고 자체 C++ 차량 물리를 구현한다.

## 빌드

공식 10.0.0 소스를 별도 디렉터리에 준비하고 필요한 타깃만 빌드한다. 아래 경로는 예시이며 저장소 안에 Chrono 소스나 빌드 결과를 커밋하지 않는다.

```sh
git clone --depth 1 --branch 10.0.0 https://github.com/projectchrono/chrono.git /path/to/chrono-10
cmake -S /path/to/chrono-10 -B /path/to/chrono-10-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_DEMOS=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_BENCHMARKING=OFF \
  -DCH_ENABLE_MODULE_VEHICLE=ON \
  -DCH_ENABLE_MODULE_VEHICLE_COSIM=OFF
cmake --build /path/to/chrono-10-build --target ChronoModels_vehicle ChronoModels_robot --parallel
```

10.0.0의 build-tree package가 Vehicle 요청에도 `ChronoModels_robot` 존재 여부를 검사하므로 스파이크 재현 시 두 타깃을 빌드한다. 자체 물리 런타임에 이 라이브러리들이 포함된다는 뜻은 아니다.

스파이크는 Chrono 빌드 트리의 CMake package를 명시해 빌드한다. Eigen을 별도 package manager에 설치했다면 그 toolchain 또는 `Eigen3_DIR`도 전달한다.

```sh
cd cpp/host
cmake -S spikes/chrono_vehicle -B build/chrono_vehicle_spike \
  -DCMAKE_BUILD_TYPE=Release \
  -DChrono_DIR=/path/to/chrono-10-build/cmake
cmake --build build/chrono_vehicle_spike --parallel
```

Windows의 다중 구성 generator에서는 두 빌드 명령에 `--config Release`를 추가한다.

## 실행

```sh
./build/chrono_vehicle_spike/chrono_vehicle_spike --duration=12 --runs=3
./build/chrono_vehicle_spike/chrono_vehicle_spike --duration=12 --runs=3 --vehicles=4 --min-rtf=1.0
./build/chrono_vehicle_spike/chrono_vehicle_spike --duration=8 --runs=3 --wall-x=35
```

기본 timestep은 60Hz 외부 tick을 정확히 10개 substep으로 나눌 수 있는 1/600초다. `--step=0.002`처럼 바꿔 성능 민감도를 비교할 수 있다.

프로그램은 반복성 허용 오차, 벽 관통·정지 여부, 선택적인 `--min-rtf` 기준을 검증하고 실패 시 0이 아닌 종료 코드를 반환한다. 짧은 반복성·충돌 회귀 시험은 다음처럼 실행한다.

```sh
ctest --test-dir build/chrono_vehicle_spike --output-on-failure
```

이 결과는 외부 엔진과 자체 구현을 비교할 때 사용하는 개발기 기준 자료다. 필요하면 목표 Windows PC(i5-10400)에서 같은 소스와 옵션으로 다시 실행한다. 자체 물리의 결정과 재검토 조건은 [ADR-006](../../../../docs/decisions/ADR-006-custom-vehicle-physics.md)에 정의한다.
