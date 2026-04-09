#include "physics/vehicle_physics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;

VehiclePhysics::VehiclePhysics(double lat, double lon, double alt, float heading) {
    state_.lat     = lat;
    state_.lon     = lon;
    state_.alt     = alt;
    state_.heading = heading;
}

void VehiclePhysics::set_input(const VehicleInput& input) {
    std::lock_guard lock(input_mutex_);
    input_ = input;
}

VehicleState VehiclePhysics::update(double dt)
{
    VehicleInput in;
    {
        std::lock_guard lock(input_mutex_);
        in = input_;
    }

    const float fdt = static_cast<float>(dt);

    // Timestamp
    auto now = std::chrono::system_clock::now();
    state_.timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    // --- Acceleration ---
    // 공기저항: 속도 방향 반대로 작용 (후진 시에도 자연스럽게 감속)
    float drag = DRAG_COEFF * state_.speed * std::abs(state_.speed);

    float net_accel;
    if (state_.speed >= 0.f) {
        // 전진 또는 정지: throttle=전진, brake=감속/후진 진입
        net_accel = in.throttle * MAX_ACCEL
                  - in.brake    * MAX_BRAKE
                  - drag;
    } else {
        // 후진 중: throttle=후진 탈출, brake=후진 가속
        net_accel = in.throttle * MAX_BRAKE
                  - in.brake    * MAX_REVERSE_ACCEL
                  - drag;
    }

    // 핸드브레이크: 현재 이동 방향 반대로 감속
    if (in.handbrake)
        net_accel -= std::copysign(HANDBRAKE_DECEL, state_.speed);

    state_.accel  = net_accel;
    state_.speed += net_accel * fdt;
    state_.speed  = std::clamp(state_.speed, -MAX_REVERSE_SPEED, MAX_SPEED);

    // --- Steering ---
    // 속도에 비례해서 조향 반응, 후진 시 방향 반전
    // 5m/s -> 18km/h 부터 최대 조항이 되도록 튜닝
    float speed_factor = std::min(std::abs(state_.speed) / 5.f, 1.f);
    // 핸들을 최대 조항으로 운전 시 초당 60도 회전 -> 60hz 에서 1 프레임에 1도 회전
    // U턴 3초 걸림
    float heading_rate = in.steering * MAX_STEER_RATE * speed_factor;
    if (state_.speed < 0.f)
        heading_rate = -heading_rate;

    // 핸드브레이크 드리프트 효과
    if (in.handbrake && std::abs(state_.speed) > 2.f)
        heading_rate *= 1.8f;

    // 360을 넘지 못하도록 설정
    state_.heading = std::fmod(state_.heading + heading_rate * fdt + 360.f, 360.f);

    // --- Roll (시각적 기울기) ---
    state_.roll = -in.steering * std::min(std::abs(state_.speed) / MAX_SPEED, 1.f) * 6.f;

    // --- Position ---
    double hr  = state_.heading * DEG2RAD;
    // 동<->서 , 남<->북 속도 나눔
    double vx  = state_.speed * std::sin(hr);
    double vy  = state_.speed * std::cos(hr);

    state_.lat += (vy / EARTH_R) * RAD2DEG * dt;
    state_.lon += (vx / (EARTH_R * std::cos(state_.lat * DEG2RAD))) * RAD2DEG * dt;

    // --- RPM ---
    state_.rpm = IDLE_RPM + (std::abs(state_.speed) / MAX_SPEED) * (MAX_RPM - IDLE_RPM);

    // --- Fuel ---
    if (state_.fuel > 0.f)
        state_.fuel = std::max(0.f, state_.fuel - in.throttle * FUEL_RATE * fdt);

    return state_;
}

VehicleState VehiclePhysics::get_state() const {
    return state_;
}
