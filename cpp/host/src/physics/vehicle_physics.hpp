#pragma once

#include "collision/collision_world.hpp"
#include "physics/suspension_model.hpp"
#include "terrain/ground_query.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

enum class VehicleGear : uint8_t {
    Neutral = 0,
    Drive   = 1,
    Reverse = 2,
};

enum class VehicleDamageZone : uint8_t {
    None = 0,
    Front = 1,
    Rear = 2,
    Left = 3,
    Right = 4,
    Roof = 5,
    Underbody = 6,
};

struct VehicleInput {
    float throttle  = 0.f;   // 0 ~ 1
    float brake     = 0.f;   // 0 ~ 1
    // Speed-independent normalized road-wheel angle target, canonical FLU.
    float steering  = 0.f;   // -1 (right lock) ~ 1 (left lock)
    bool  handbrake = false;
    VehicleGear gear = VehicleGear::Drive;
};

struct VehicleParameters {
    float mass_kg                 = 1500.f;
    float wheelbase_m             = 2.70f;
    float max_steering_angle_rad  = 0.61086524f; // 35 degrees
    float steering_rate_rad_s     = 1.80f;
    float steering_return_rate_rad_s = 2.20f;
    float max_drive_force_n       = 6000.f;
    float max_reverse_force_n     = 3600.f;
    float max_drive_power_w       = 100000.f;
    // The accelerator requests force through a finite-response powertrain.
    // Rise and fall rates prevent keyboard input from becoming an impulse.
    float drive_force_rise_rate_n_per_s = 18000.f;
    float drive_force_fall_rate_n_per_s = 36000.f;
    float max_service_brake_n     = 18000.f;
    float max_handbrake_force_n   = 22000.f;
    float rolling_resistance_coeff = 0.015f;
    float drivetrain_drag_n_per_mps = 45.f;
    float drag_coefficient        = 0.29f;
    float frontal_area_m2         = 2.20f;
    float air_density_kg_m3       = 1.225f;
    float tire_radius_m           = 0.32f;
    float final_drive_ratio       = 3.42f;
    float drive_gear_ratio        = 3.50f;
    float reverse_gear_ratio      = 3.20f;
    float max_forward_speed_mps   = 50.f;
    float max_reverse_speed_mps   = 8.f;
    float idle_rpm                = 800.f;
    float max_rpm                 = 7000.f;
    float fuel_rate_percent_s     = 0.004f;
    float front_track_m           = 1.58f;
    float rear_track_m            = 1.58f;
    float cg_height_m             = 0.55f;
    // Static front-axle normal-load share. This also defines the CG's
    // longitudinal position: distance from CG to rear axle = share * wheelbase.
    float front_static_load_fraction = 0.55f;
    // Fraction of driveline force sent to the front axle (0=RWD, 1=FWD).
    float front_drive_torque_fraction = 0.f;
    // Fraction of service-brake force assigned to the front axle.
    float front_service_brake_fraction = 0.65f;
    float yaw_inertia_kg_m2       = 2850.f;
    float pitch_inertia_kg_m2     = 2200.f;
    float roll_inertia_kg_m2      = 700.f;
    // Reserved v4 compatibility fields. Both must remain zero: the four
    // suspension springs/dampers provide all physical pitch/roll stiffness and
    // damping, with no synthetic body-to-ground attitude spring.
    float attitude_spring_n_m_rad = 0.f;
    float attitude_damping_n_m_s_rad = 0.f;
    // Per-tire linear cornering stiffness. Axle-specific values calibrate the
    // handling balance; normal load and friction still bound each tire force.
    float front_tire_corner_stiffness_n_rad = 60000.f;
    float rear_tire_corner_stiffness_n_rad = 50000.f;
    float tire_longitudinal_stiffness_n = 90000.f;
    float tire_friction           = 1.05f;
    // Vehicle/tire-specific tuning layered over the terrain-authored
    // multiplier in GroundHit. Unknown future material IDs use default scale.
    float surface_default_friction_scale = 1.0f;
    float surface_asphalt_friction_scale = 1.0f;
    float surface_low_friction_scale = 1.0f;
    float surface_rough_friction_scale = 1.0f;
    // Lateral force is allocated first. Longitudinal drive/brake force may
    // only use the friction-circle reserve left after this fraction.
    float lateral_grip_priority   = 0.95f;
    float traction_control_slip_target = 0.08f;
    float traction_control_full_cut_slip = 0.12f;
    float wheel_inertia_kg_m2     = 1.8f;
    float wheel_free_spin_damping_n_m_s = 3.6f;
    // Regularizes slip angle near zero speed without disabling lateral grip.
    float low_speed_slip_reference_mps = 1.5f;
    simcore_host::SuspensionParameters suspension;
};

[[nodiscard]] bool valid_vehicle_parameters(
    const VehicleParameters& parameters);

struct Vector3State {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct WheelState {
    // 0=front-left, 1=front-right, 2=rear-left, 3=rear-right.
    uint32_t wheel_index = 0;
    bool in_contact = false;
    float steering_angle = 0.f; // radians, positive = left
    float angular_speed = 0.f;
    float normal_load = 0.f;
    float longitudinal_slip = 0.f;
    float slip_angle = 0.f;
    float longitudinal_force = 0.f;
    float lateral_force = 0.f; // wheel-local left-positive force
    Vector3State contact_point_enu;
    Vector3State contact_normal_enu{0.0, 0.0, 1.0};
};

// Force-accounting diagnostics, separate from the published vehicle protocol.
// Forces are those used by the pre-integration tire sample. The impulse is the
// actual sum of velocity-constraint reactions over the accepted tick, not a
// positional correction or a sum of intermediate solver iterates. These are
// histories, unlike WheelState's final contact geometry after integration.
struct WheelContactSupportDiagnostics {
    float suspension_normal_force_n = 0.f;
    double hard_stop_impulse_n_s = 0.0;
    // One-step estimate from the previous accepted tick's impulse / its dt,
    // usable only while this corner still touches its compression stop.
    float tire_hard_stop_normal_force_n = 0.f;
};

struct VehicleState {
    uint32_t entity_id = 1;
    double   timestamp = 0.0;
    double   lat       = 0.0;
    double   lon       = 0.0;
    double   alt       = 0.0;
    float    heading   = 0.f;  // degrees 0~360
    float    pitch     = 0.f;  // canonical body-frame pitch (degrees)
    float    roll      = 0.f;  // canonical body-frame roll (degrees)
    float    speed     = 0.f;  // m/s
    float    accel     = 0.f;  // m/s²
    float    fuel      = 100.f; // %
    float    rpm       = 800.f;
    double   east      = 0.0;   // local ENU X (meters)
    double   north     = 0.0;   // local ENU Y (meters)
    float    yaw_rate  = 0.f;   // FLU body-Z angular rate (rad/s), positive = left turn
    // Bicycle-equivalent centre steering angle, not either individual front
    // wheel. WheelState carries the separate Ackermann angles for rendering.
    float    steering_angle = 0.f; // radians, positive = left
    VehicleGear gear   = VehicleGear::Drive;
    float damage_percent = 0.f;
    float last_impact_impulse_n_s = 0.f;
    VehicleDamageZone damage_zone = VehicleDamageZone::None;
    uint32_t collision_event_sequence = 0;
    float collision_half_length_m = 0.f;
    float collision_half_width_m = 0.f;
    float collision_half_height_m = 0.f;
    Vector3State position_enu;
    // Published body vectors use the right-handed canonical frame:
    // X=forward, Y=left, Z=up. Internal solver lateral values are right-positive.
    Vector3State linear_velocity_body;
    Vector3State angular_velocity_body;
    std::array<WheelState, 4> wheels;
};

class VehiclePhysics {
public:
    VehiclePhysics(double lat, double lon, double alt, float heading,
                   VehicleParameters parameters = {},
                   std::shared_ptr<const simcore_host::GroundQuery> ground_query = {},
                   std::shared_ptr<const simcore_host::CollisionWorld> collision_world = {});

    void         set_input(const VehicleInput& input);
    void         reset();
    // Install one fully verified MapPackage collision snapshot. SimulationHost
    // calls this only at a fixed-tick boundary and the vehicle is reset before
    // the new environment can be observed by an update.
    void         replace_environment(
        std::shared_ptr<const simcore_host::GroundQuery> ground_query,
        std::shared_ptr<const simcore_host::CollisionWorld> collision_world);
    VehicleState update(double dt);
    VehicleState update(
        double dt,
        std::vector<simcore_host::KinematicCollisionProxy> dynamic_proxies);
    VehicleState get_state() const;
    const std::vector<simcore_host::KinematicCollisionProxy>&
        get_last_resolved_dynamic_proxies() const noexcept
    {
        return last_resolved_dynamic_proxies_;
    }
    const std::vector<simcore_host::CollisionContact>&
        get_last_collision_contacts() const noexcept
    {
        return last_collision_contacts_;
    }
    std::array<WheelContactSupportDiagnostics, 4>
        get_wheel_contact_support_diagnostics() const;

private:
    bool update_wheel_contacts(float dt_seconds, bool update_suspension,
                               bool enforce_non_penetration = false,
                               bool update_attitude_target = false,
                               std::array<double, 4>* tick_hard_stop_impulses = nullptr);
    bool resolve_non_penetrating_ground_contacts(
        bool update_suspension,
        std::array<double, 4>* tick_hard_stop_impulses = nullptr);

    VehicleState       state_;
    VehicleInput       input_;
    VehicleParameters  parameters_;
    std::shared_ptr<const simcore_host::GroundQuery> ground_query_;
    std::shared_ptr<const simcore_host::CollisionWorld> collision_world_;
    mutable std::mutex input_mutex_;

    double origin_lat_  = 0.0;
    double origin_lon_  = 0.0;
    double origin_alt_  = 0.0;
    double east_m_      = 0.0;
    double north_m_     = 0.0;
    double spawn_heading_rad_ = 0.0;
    double heading_rad_ = 0.0;
    float body_longitudinal_speed_mps_ = 0.f;
    float solver_lateral_speed_mps_ = 0.f; // private solver Y, positive = right
    float solver_yaw_rate_rad_s_ = 0.f; // private heading rate, positive = clockwise
    float solver_road_wheel_angle_rad_ = 0.f;
    float pitch_rad_ = 0.f;
    float roll_rad_ = 0.f;
    float pitch_rate_rad_s_ = 0.f;
    float roll_rate_rad_s_ = 0.f;
    float vertical_speed_mps_ = 0.f;
    float ground_pitch_rad_ = 0.f;
    float ground_roll_rad_ = 0.f;
    // Terrain attitude is the contact-normal basis used for gravity/motion.
    // Support attitude is fitted through the four wheel centres and is used
    // only to initialize/reset chassis pitch/roll (and for diagnostics).
    float support_pitch_rad_ = 0.f;
    float support_roll_rad_ = 0.f;
    bool support_attitude_valid_ = false;
    float applied_drive_force_n_ = 0.f;
    std::array<float, 4> wheel_angular_speed_rad_s_{};
    std::array<float, 4> suspension_compression_m_{};
    std::array<float, 4> suspension_base_force_n_{};
    std::array<bool, 4> suspension_had_contact_{};
    std::array<WheelContactSupportDiagnostics, 4> wheel_contact_support_{};
    std::array<float, 4> previous_hard_stop_normal_force_n_{};
    std::array<bool, 4> compression_stop_active_{};
    // Number of geometrically valid downward surface hits in the most recent
    // contact sample. This is distinct from suspension contact so map coverage
    // loss is not confused with a wheel that is merely above full droop.
    std::size_t ground_query_hit_count_ = 0;
    // Per-corner authored-surface coverage from the same final contact sample.
    // A complete axle or side leaving the finite MapPackage is terminal for
    // this ground-bound reduced model, not an airborne state that may continue
    // rotating on the remaining two springs.
    std::array<bool, 4> ground_surface_hit_by_wheel_{};
    std::array<simcore_host::GroundSurfaceMaterialId, 4>
        ground_surface_material_by_wheel_{};
    std::array<float, 4> ground_friction_multiplier_by_wheel_{};
    // Committed only when the complete ground-bound vehicle step is accepted.
    // SimulationHost consumes this tick-local feedback to commit finite runtime
    // entity reactions without giving Unreal a second physics authority.
    std::vector<simcore_host::KinematicCollisionProxy>
        last_resolved_dynamic_proxies_;
    std::vector<simcore_host::CollisionContact> last_collision_contacts_;

    static constexpr float  STOP_EPSILON     = 0.01f;
    static constexpr double EARTH_R          = 6371000.0; // meters
};
