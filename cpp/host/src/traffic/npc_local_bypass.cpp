#include "traffic/npc_local_bypass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace simcore_host {
class NpcLocalRoadIndex {
public:
    explicit NpcLocalRoadIndex(const TrafficNetwork& network)
    {
        for (const auto& lane:network.lanes) for (std::size_t i=1;i<lane.points.size();++i) {
            const auto& a=lane.points[i-1];
            const auto& b=lane.points[i];
            const double de=b.east_m-a.east_m,dn=b.north_m-a.north_m,length_squared=de*de+dn*dn;
            if (length_squared<1.e-8) continue;
            const double radius=lane.width_m*0.5+0.03;
            const Segment segment{a,b,radius,length_squared};
            for (int e=cell(std::min(a.east_m,b.east_m)-radius);e<=cell(std::max(a.east_m,b.east_m)+radius);++e)
                for (int n=cell(std::min(a.north_m,b.north_m)-radius);n<=cell(std::max(a.north_m,b.north_m)+radius);++n)
                    cells_[key(e,n)].push_back(segment);
        }
    }
    std::pair<bool,bool> directions(const GroundPointEnu& point,double heading) const
    {
        const auto found=cells_.find(key(cell(point.east_m),cell(point.north_m)));
        if (found==cells_.end()) return {};
        bool same=false,opposing=false,same_to_left=false,same_to_right=false;
        for (const auto& segment:found->second) {
            const auto& a=segment.a; const auto& b=segment.b;
            const double de=b.east_m-a.east_m,dn=b.north_m-a.north_m;
            // Rounded segment ends keep the same-flow strip continuous at
            // curved polyline joints; separate rectangular strips leave holes.
            const double projection=std::clamp(((point.east_m-a.east_m)*de
                +(point.north_m-a.north_m)*dn)/segment.length_squared,0.0,1.0);
            if (std::abs(point.up_m-a.up_m-projection*(b.up_m-a.up_m))>0.5) continue;
            const double distance=std::hypot(point.east_m-a.east_m-projection*de,
                point.north_m-a.north_m-projection*dn);
            const double lateral=((point.east_m-a.east_m)*dn-(point.north_m-a.north_m)*de)
                /std::sqrt(segment.length_squared);
            const double direction=(de*std::sin(heading)+dn*std::cos(heading))/std::sqrt(segment.length_squared);
            // Bridge only small gaps BETWEEN same-flow lanes. Padding the
            // whole carriageway would also extend its left edge across yellow.
            if (direction>0.5 && distance<=segment.radius+0.15) {
                same_to_left|=lateral>=0.0;
                same_to_right|=lateral<=0.0;
            }
            if (distance>segment.radius) continue;
            same|=direction>0.5;
            opposing|=direction< -0.5;
        }
        return {same || (same_to_left && same_to_right),opposing};
    }
private:
    struct Segment { GroundPointEnu a,b; double radius,length_squared; };
    static int cell(double coordinate) { return static_cast<int>(std::floor(coordinate/8.0)); }
    static std::uint64_t key(int east,int north) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(east))<<32)
            |static_cast<std::uint32_t>(north);
    }
    std::unordered_map<std::uint64_t,std::vector<Segment>> cells_;
};

std::shared_ptr<const NpcLocalRoadIndex> make_npc_local_road_index(
    const TrafficNetwork& network)
{
    return std::make_shared<const NpcLocalRoadIndex>(network);
}

namespace {
constexpr double pi = std::numbers::pi;
constexpr double sample_step_m = 0.25;

double blend(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t*t*t*(10.0 + t*(-15.0 + 6.0*t));
}

bool finite(const GroundPointEnu& p)
{
    return std::isfinite(p.east_m) && std::isfinite(p.north_m) && std::isfinite(p.up_m);
}

// Default-material packages have no road/sidewalk classification. For those,
// use same-flow lane footprints, not an individual lane or merge window.
// Classified asphalt may use right-side room, never unknown space toward the
// centreline: painted yellow can lie in a gap before the opposing footprint.
bool road_point(const NpcLocalRoadIndex& road_index, const GroundQuery& ground,
                const GroundPointEnu& point, const GroundPointEnu& reference,
                double reference_heading)
{
    const auto hit = ground.query_down({{point.east_m,point.north_m,point.up_m+1.0},2.0});
    if (!hit || !finite(hit->point_enu) || !finite(hit->normal_enu)
        || hit->normal_enu.up_m < 0.85
        || std::abs(hit->point_enu.up_m-point.up_m) > 0.18) return false;
    if (hit->surface_material_id == GroundSurfaceMaterialId::Rough) return false;
    const auto [same_direction_road,opposing_road]=road_index.directions(point,reference_heading);
    if (opposing_road && !same_direction_road) return false;
    const double right_offset=(point.east_m-reference.east_m)*std::cos(reference_heading)
        -(point.north_m-reference.north_m)*std::sin(reference_heading);
    const bool classified_road=hit->surface_material_id == GroundSurfaceMaterialId::Asphalt
        || hit->surface_material_id == GroundSurfaceMaterialId::LowFriction;
    return (classified_road && (same_direction_road || right_offset>=0.0))
        || (hit->surface_material_id == GroundSurfaceMaterialId::Default && same_direction_road);
}
} // namespace

std::optional<NpcLaneSample> NpcLocalBypassPlan::geometry(double distance_m) const
{
    if (!std::isfinite(distance_m) || distance_m < -1.e-6) return std::nullopt;
    distance_m=std::max(0.0,distance_m);
    auto sample=reference_.sample_ahead(distance_m);
    if (!sample) return std::nullopt;
    if (distance_m >= return_begin_m_+transition_m_ || distance_m <= 1.e-8) return sample;
    const double offset=lateral_m_*blend(distance_m/transition_m_)
        *(1.0-blend((distance_m-return_begin_m_)/transition_m_));
    const auto before=reference_.sample_ahead(std::max(0.0,distance_m-0.5));
    const auto after=reference_.sample_ahead(distance_m+0.5);
    if (!before || !after) return std::nullopt;
    const double heading=std::atan2(after->position_enu.east_m-before->position_enu.east_m,
        after->position_enu.north_m-before->position_enu.north_m);
    sample->position_enu.east_m += std::cos(heading)*offset;
    sample->position_enu.north_m -= std::sin(heading)*offset;
    return sample;
}

std::optional<NpcLaneSample> NpcLocalBypassPlan::sample(double travelled_m) const
{
    if (!ground_ || !road_index_) return std::nullopt;
    const double distance=travelled_m-begin_m_;
    auto sample=geometry(distance);
    const auto reference=reference_.sample_ahead(std::max(0.0,distance));
    if (!sample || !reference) return std::nullopt;
    if (distance>1.e-8 && !complete(travelled_m)) {
        const auto before=geometry(std::max(0.0,distance-0.25));
        const auto after=geometry(distance+0.25);
        if (!before || !after) return std::nullopt;
        sample->heading_deg=std::atan2(after->position_enu.east_m-before->position_enu.east_m,
            after->position_enu.north_m-before->position_enu.north_m)*180.0/pi;
    }
    const auto hit=ground_->query_down({{sample->position_enu.east_m,sample->position_enu.north_m,
        sample->position_enu.up_m+1.0},2.0});
    if (!hit || !finite(hit->point_enu)
        || std::abs(hit->point_enu.up_m-sample->position_enu.up_m)>0.18) return std::nullopt;
    sample->position_enu=hit->point_enu;
    const double heading=sample->heading_deg*pi/180.0;
    const double reference_heading=reference->heading_deg*pi/180.0;
    // Cover the whole underbody, including side midpoints: four tyres alone
    // could accept a narrow support hole, curb or sidewalk under the chassis.
    const int longitudinal=std::max(2,static_cast<int>(std::ceil(dimensions_.half_length_m*2.0)));
    const int lateral=std::max(2,static_cast<int>(std::ceil(dimensions_.half_width_m*2.0)));
    for (int i=0;i<=longitudinal;++i) {
        const double f=dimensions_.half_length_m*(2.0*i/longitudinal-1.0);
        for (int j=0;j<=lateral;++j) {
            const double s=dimensions_.half_width_m*(2.0*j/lateral-1.0);
            const GroundPointEnu p{sample->position_enu.east_m+std::sin(heading)*f+std::cos(heading)*s,
                sample->position_enu.north_m+std::cos(heading)*f-std::sin(heading)*s,
                sample->position_enu.up_m};
            if (!road_point(*road_index_,*ground_,p,reference->position_enu,reference_heading)) return std::nullopt;
        }
    }
    return sample;
}

bool NpcLocalBypassPlan::complete(double travelled_m) const noexcept
{
    return ground_ && std::isfinite(travelled_m) && travelled_m >= end_m();
}

std::uint32_t NpcLocalBypassPlan::turn_signal_intent(double travelled_m) const noexcept
{
    if (!ground_ || !std::isfinite(travelled_m) || travelled_m<begin_m_ || complete(travelled_m)) return 0;
    const double distance=travelled_m-begin_m_;
    if (distance<transition_m_) return lateral_m_<0.0 ? 1U : 2U;
    if (distance>=return_begin_m_) return lateral_m_<0.0 ? 2U : 1U;
    return 0; // Parallel passing portion: no lateral movement is requested.
}

bool NpcLocalBypassPlan::opposing_footprint(const NpcLaneSample& sample) const
{
    const double heading=sample.heading_deg*pi/180.0;
    for (const double front : {-dimensions_.half_length_m,dimensions_.half_length_m})
        for (const double side : {-dimensions_.half_width_m,dimensions_.half_width_m}) {
            auto point=sample.position_enu;
            point.east_m+=std::sin(heading)*front+std::cos(heading)*side;
            point.north_m+=std::cos(heading)*front-std::sin(heading)*side;
            const auto [same,opposing]=road_index_->directions(point,heading);
            if (opposing && !same) return true;
        }
    return false;
}

double NpcLocalBypassPlan::extra_stop_margin_m() const noexcept
{
    return std::hypot(dimensions_.half_length_m,dimensions_.half_width_m)
        -dimensions_.half_length_m+0.15;
}

void NpcLocalBypassSearch::begin(
    const TrafficNetwork& network, const GroundQuery& ground,
    const NpcLaneFollower& follower, double last_blocked_distance_m,
    const NpcLocalBypassDimensions& dimensions,
    std::shared_ptr<const NpcLocalRoadIndex> road_index)
{
    *this = NpcLocalBypassSearch{};
    if (!follower.state().valid || !std::isfinite(last_blocked_distance_m)
        || last_blocked_distance_m<0.0 || last_blocked_distance_m>40.0
        || !std::isfinite(dimensions.half_length_m) || dimensions.half_length_m<=0.0
        || dimensions.half_length_m>5.0 || !std::isfinite(dimensions.half_width_m)
        || dimensions.half_width_m<=0.0 || dimensions.half_width_m>2.0
        || !std::isfinite(dimensions.minimum_turn_radius_m)
        || dimensions.minimum_turn_radius_m<2.0 || dimensions.minimum_turn_radius_m>15.0)
        return;
    reference_ = follower;
    ground_ = &ground;
    road_index_ = road_index ? std::move(road_index) : make_npc_local_road_index(network);
    dimensions_ = dimensions;
    last_blocked_distance_m_ = last_blocked_distance_m;
    status_ = NpcLocalBypassSearchStatus::Pending;
}

bool NpcLocalBypassSearch::prepare_candidate()
{
    constexpr std::array<double, 3> length_scales{1.0, 1.25, 1.5};
    while (next_candidate_ < 60) {
        const auto index = next_candidate_++;
        const double offset = 0.75 + static_cast<int>(index / 6) * 0.5;
        const double length_scale = length_scales[(index / 2) % 3];
        const double transition = std::max(5.0,
            std::sqrt(6.2 * offset * dimensions_.minimum_turn_radius_m)) * length_scale;
        const double side = index % 2 == 0 ? -1.0 : 1.0;
        candidate_ = NpcLocalBypassPlan{};
        candidate_.reference_ = reference_;
        candidate_.ground_ = ground_;
        candidate_.road_index_ = road_index_;
        candidate_.dimensions_ = dimensions_;
        candidate_.begin_m_ = reference_.state().distance_travelled_m;
        candidate_.lateral_m_ = offset * side;
        candidate_.transition_m_ = transition;
        candidate_.return_begin_m_ = std::max(transition, last_blocked_distance_m_ + 1.0);
        length_ = candidate_.end_m() - candidate_.begin_m_;
        if (length_ > 60.0) continue;
        sample_count_ = static_cast<int>(std::ceil(length_ / sample_step_m));
        sample_index_ = 0;
        prediction_index_ = 0;
        maximum_stretch_ = 1.0;
        previous_.reset();
        samples_.clear();
        samples_.reserve(sample_count_ + 1);
        phase_ = Phase::InitialSample;
        return true;
    }
    return false;
}

NpcLocalBypassSearchProgress NpcLocalBypassSearch::advance(
    const std::function<bool(const NpcLaneSample&, double)>& clear,
    std::size_t max_samples)
{
    if (status_ != NpcLocalBypassSearchStatus::Pending || max_samples == 0)
        return {status_, 0};
    if (!clear) {
        status_ = NpcLocalBypassSearchStatus::Exhausted;
        return {status_, 0};
    }
    std::size_t checked = 0;
    while (checked < max_samples && status_ == NpcLocalBypassSearchStatus::Pending) {
        if (phase_ == Phase::Candidate) {
            if (!prepare_candidate()) status_ = NpcLocalBypassSearchStatus::Exhausted;
            continue;
        }
        ++checked;
        if (phase_ == Phase::InitialSample) {
            previous_ = candidate_.sample(candidate_.begin_m_);
            phase_ = previous_ ? Phase::Geometry : Phase::Candidate;
        } else if (phase_ == Phase::Geometry) {
            const double distance = length_ * sample_index_ / sample_count_;
            const auto sample = candidate_.sample(candidate_.begin_m_ + distance);
            if (!sample || candidate_.opposing_footprint(*sample) || !clear(*sample, 0.0)) {
                phase_ = Phase::Candidate;
                continue;
            }
            samples_.push_back(*sample);
            candidate_.borrows_opposing_space_ |= candidate_.opposing_footprint(*sample);
            if (sample_index_ > 0) {
                const double moved = std::hypot(
                    sample->position_enu.east_m - previous_->position_enu.east_m,
                    sample->position_enu.north_m - previous_->position_enu.north_m);
                const double rotated = std::abs(std::remainder(
                    (sample->heading_deg - previous_->heading_deg) * pi / 180.0, 2.0 * pi));
                if (moved < 1.e-8 || rotated > moved / dimensions_.minimum_turn_radius_m + 0.004) {
                    phase_ = Phase::Candidate;
                    continue;
                }
                maximum_stretch_ = std::max(maximum_stretch_, moved / (length_ / sample_count_));
            }
            previous_ = sample;
            if (++sample_index_ > sample_count_) {
                candidate_.route_speed_limit_mps_ = std::min(
                    reference_.config().max_speed_mps, 3.0 / maximum_stretch_);
                candidate_.expected_duration_s_ = length_ / candidate_.route_speed_limit_mps_
                    + candidate_.route_speed_limit_mps_ / reference_.config().acceleration_mps2 + 2.0;
                phase_ = candidate_.borrows_opposing_space_ && candidate_.expected_duration_s_ > 30.0
                    ? Phase::Candidate : Phase::Prediction;
            }
        } else {
            if (!clear(samples_[prediction_index_], candidate_.expected_duration_s_)) {
                phase_ = Phase::Candidate;
                continue;
            }
            if (++prediction_index_ == samples_.size()) {
                plan_ = std::move(candidate_);
                status_ = NpcLocalBypassSearchStatus::Found;
            }
        }
    }
    total_samples_checked_ += checked;
    return {status_, checked};
}

std::optional<NpcLocalBypassPlan> plan_npc_local_bypass(
    const TrafficNetwork& network, const GroundQuery& ground,
    const NpcLaneFollower& follower, double last_blocked_distance_m,
    const NpcLocalBypassDimensions& dimensions,
    const std::function<bool(const NpcLaneSample&, double)>& clear)
{
    if (!clear) return std::nullopt;
    NpcLocalBypassSearch search;
    search.begin(network, ground, follower, last_blocked_distance_m, dimensions);
    while (search.status() == NpcLocalBypassSearchStatus::Pending)
        static_cast<void>(search.advance(clear, 256));
    return search.plan();
}
} // namespace simcore_host
