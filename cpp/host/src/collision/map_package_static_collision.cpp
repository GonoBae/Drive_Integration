#include "collision/map_package_static_collision.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace simcore_host {
namespace {

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::vector<std::string> split_csv_row(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto separator = line.find(',', start);
        fields.push_back(trim(std::string_view(line).substr(
            start,
            separator == std::string::npos
                ? std::string::npos : separator - start)));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return fields;
}

double parse_finite_double(
    const std::filesystem::path& path,
    std::size_t line_number,
    const std::string& text)
{
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0'
        || !std::isfinite(value)) {
        throw std::runtime_error(
            path.string() + ":" + std::to_string(line_number)
            + " contains an invalid finite number: " + text);
    }
    return value;
}

bool valid_id(const std::string& value)
{
    return !value.empty() && value.size() <= 128
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '_' || character == '-' || character == '.';
        });
}

StaticColliderSemantic parse_semantic(
    const std::filesystem::path& path,
    std::size_t line_number,
    const std::string& text)
{
    if (text == "wall") {
        return StaticColliderSemantic::Wall;
    }
    if (text == "curb") {
        return StaticColliderSemantic::Curb;
    }
    if (text == "barrier") {
        return StaticColliderSemantic::Barrier;
    }
    throw std::runtime_error(
        path.string() + ":" + std::to_string(line_number)
        + " has an unsupported collider semantic: " + text);
}

} // namespace

CollisionWorld load_static_collision_world(
    const MapPackageManifest& manifest)
{
    const std::string filename(kStaticCollidersFileName);
    if (std::find(
            manifest.collision_files.begin(),
            manifest.collision_files.end(),
            filename) == manifest.collision_files.end()) {
        throw std::runtime_error(
            "MapPackage manifest does not declare " + filename);
    }

    const auto path = manifest.package_directory / filename;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "MapPackage static collision payload not found: " + path.string());
    }

    static const std::vector<std::string> expected_header{
        "collider_id", "semantic", "shape",
        "center_e_m", "center_n_m", "center_u_m", "heading_rad",
        "half_length_m", "half_width_m", "half_height_m",
        "friction", "restitution"};

    std::vector<StaticObbCollider> colliders;
    std::unordered_set<std::string> collider_ids;
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_row(line);
        if (!header_seen) {
            if (fields != expected_header) {
                throw std::runtime_error(
                    path.string() + ":" + std::to_string(line_number)
                    + " has an unsupported static_colliders.csv header");
            }
            header_seen = true;
            continue;
        }

        if (fields.size() != expected_header.size()) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " must contain exactly 12 fields");
        }
        if (!valid_id(fields[0])) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " has an invalid collider_id: " + fields[0]);
        }
        if (!collider_ids.insert(fields[0]).second) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " contains duplicate collider_id: " + fields[0]);
        }
        if (fields[2] != "obb") {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " has an unsupported static collider shape: " + fields[2]);
        }

        StaticObbCollider collider;
        collider.collider_id = fields[0];
        collider.semantic = parse_semantic(path, line_number, fields[1]);
        collider.shape.center_enu = {
            parse_finite_double(path, line_number, fields[3]),
            parse_finite_double(path, line_number, fields[4])};
        collider.shape.center_up_m = parse_finite_double(
            path, line_number, fields[5]);
        collider.shape.heading_rad = parse_finite_double(
            path, line_number, fields[6]);
        collider.shape.half_length_m = parse_finite_double(
            path, line_number, fields[7]);
        collider.shape.half_width_m = parse_finite_double(
            path, line_number, fields[8]);
        collider.shape.half_height_m = parse_finite_double(
            path, line_number, fields[9]);
        collider.material.friction = parse_finite_double(
            path, line_number, fields[10]);
        collider.material.restitution = parse_finite_double(
            path, line_number, fields[11]);

        if (collider.shape.half_length_m <= 0.0
            || collider.shape.half_width_m <= 0.0
            || collider.shape.half_height_m <= 0.0) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " requires positive OBB half extents");
        }
        if (collider.material.friction < 0.0
            || collider.material.restitution < 0.0
            || collider.material.restitution > 1.0) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " has invalid friction or restitution");
        }
        colliders.push_back(std::move(collider));
    }

    if (!header_seen) {
        throw std::runtime_error(
            "MapPackage static collision payload is missing its header: "
            + path.string());
    }
    return CollisionWorld(std::move(colliders));
}

} // namespace simcore_host
