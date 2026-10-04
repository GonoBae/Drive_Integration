#pragma once

#include "terrain/ground_query.hpp"
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace simcore_host {
struct CapturedGroundQuery {
    GroundQueryRequest request;
    std::optional<GroundHit> hit;
};
struct GroundQueryFrame {
    std::uint64_t simulation_time_ns = 0;
    std::size_t calls = 0, discarded = 0;
    std::vector<CapturedGroundQuery> samples;
};

// Simulation-thread observer. It forwards every query unchanged; it is not a
// cache and never supplies a previous measurement to the vehicle solver.
class CapturingGroundQuery final : public GroundQuery {
public:
    static constexpr std::size_t MaxSamples = 512;
    explicit CapturingGroundQuery(std::shared_ptr<const GroundQuery> source)
        : source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Ground capture requires a source");
        frame_.samples.reserve(MaxSamples);
    }
    void begin_frame(std::uint64_t simulation_time_ns) {
        frame_.simulation_time_ns = simulation_time_ns;
        frame_.calls = frame_.discarded = 0;
        frame_.samples.clear();
        keys_.clear();
        capturing_ = true;
    }
    void end_frame() { capturing_ = false; }
    void clear() { begin_frame(0); end_frame(); }
    const GroundQueryFrame& frame() const { return frame_; }

    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override {
        const auto hit = source_->query_down(request);
        if (!capturing_) return hit;
        ++frame_.calls;
        const std::array<double, 4> key{request.origin_enu.east_m,
            request.origin_enu.north_m, request.origin_enu.up_m, request.max_distance_m};
        for (const double value : key) {
            if (!std::isfinite(value)) { ++frame_.discarded; return hit; }
        }
        if (keys_.contains(key)) return hit;
        if (frame_.samples.size() >= MaxSamples) { ++frame_.discarded; return hit; }
        keys_.emplace(key, frame_.samples.size());
        frame_.samples.push_back({request, hit});
        return hit;
    }
private:
    std::shared_ptr<const GroundQuery> source_;
    bool capturing_ = false;
    mutable GroundQueryFrame frame_;
    mutable std::map<std::array<double, 4>, std::size_t> keys_;
};
}
