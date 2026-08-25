#pragma once

#include <algorithm>
#include <cmath>

namespace simcore_host {

struct SuspensionParameters {
    float rest_length_m = 0.35f;
    float max_compression_m = 0.15f;
    float max_extension_m = 0.15f;
    // With the default 0.55 m CG height and 0.32 m tire radius, the
    // resulting 0.12 m static compression supports one quarter of 1500 kg.
    float spring_rate_n_per_m = 30645.78125f;
    float damper_rate_n_s_per_m = 3500.f;
    float max_force_n = 12000.f;
};

struct SuspensionResult {
    float bounded_length_m = 0.f;
    float compression_m = 0.f;
    float compression_velocity_mps = 0.f;
    float normal_force_n = 0.f;
};

[[nodiscard]] inline bool valid_suspension_parameters(
    const SuspensionParameters& parameters)
{
    return std::isfinite(parameters.rest_length_m)
        && std::isfinite(parameters.max_compression_m)
        && std::isfinite(parameters.max_extension_m)
        && std::isfinite(parameters.spring_rate_n_per_m)
        && std::isfinite(parameters.damper_rate_n_s_per_m)
        && std::isfinite(parameters.max_force_n)
        && parameters.rest_length_m > 0.f
        && parameters.max_compression_m >= 0.f
        && parameters.max_extension_m >= 0.f
        && parameters.rest_length_m >= parameters.max_compression_m
        && parameters.spring_rate_n_per_m > 0.f
        && parameters.damper_rate_n_s_per_m >= 0.f
        && parameters.max_force_n > 0.f;
}

// A deterministic one-dimensional spring/damper foundation. measured_length
// is the distance from the suspension mount to the wheel centre. Chassis
// heave and full 6DoF constraint solving intentionally remain outside R1.
[[nodiscard]] inline SuspensionResult evaluate_suspension(
    const SuspensionParameters& parameters,
    float measured_length_m,
    float previous_compression_m,
    bool had_previous_contact,
    float dt_seconds)
{
    const float minimum_length =
        parameters.rest_length_m - parameters.max_compression_m;
    const float maximum_length =
        parameters.rest_length_m + parameters.max_extension_m;
    const float bounded_length = std::clamp(
        measured_length_m, minimum_length, maximum_length);
    const float compression = parameters.rest_length_m - bounded_length;
    const float compression_velocity = had_previous_contact && dt_seconds > 0.f
        ? (compression - previous_compression_m) / dt_seconds
        : 0.f;
    const float spring_force = parameters.spring_rate_n_per_m
        * std::max(0.f, compression);
    const float damper_force = parameters.damper_rate_n_s_per_m
        * compression_velocity;

    return {
        bounded_length,
        compression,
        compression_velocity,
        std::clamp(spring_force + damper_force, 0.f, parameters.max_force_n)};
}

} // namespace simcore_host
