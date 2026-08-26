#include "terrain/map_package_manifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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

void hash_bytes(std::uint64_t& hash, const char* bytes, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<unsigned char>(bytes[index]);
        hash *= 1099511628211ull;
    }
}

std::vector<std::string> parse_collision_files(const std::string& text)
{
    std::vector<std::string> files;
    std::unordered_set<std::string> seen;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto separator = text.find(',', start);
        const auto file = trim(std::string_view(text).substr(
            start,
            separator == std::string::npos
                ? std::string::npos : separator - start));
        if (file.empty()) {
            throw std::runtime_error(
                "MapPackage collision_files contains an empty entry");
        }
        const std::filesystem::path relative(file);
        const bool safe_leaf = relative.is_relative()
            && relative.filename() == relative
            && file != "." && file != ".."
            && std::all_of(file.begin(), file.end(), [](unsigned char character) {
                return std::isalnum(character) || character == '_'
                    || character == '-' || character == '.';
            });
        if (!safe_leaf) {
            throw std::runtime_error(
                "MapPackage collision file must be a safe relative filename: "
                + file);
        }
        if (!seen.insert(file).second) {
            throw std::runtime_error(
                "Duplicate MapPackage collision file: " + file);
        }
        files.push_back(file);
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    if (files.empty() || files.size() > 32) {
        throw std::runtime_error(
            "MapPackage collision_files must contain between 1 and 32 files");
    }
    if (std::find(files.begin(), files.end(), "ground_surface.csv")
        == files.end()) {
        throw std::runtime_error(
            "MapPackage collision_files must include ground_surface.csv");
    }
    return files;
}

} // namespace

std::string compute_map_package_collision_checksum(
    const std::filesystem::path& package_directory,
    const std::vector<std::string>& collision_files)
{
    std::uint64_t hash = 14695981039346656037ull;
    constexpr char separator = '\0';
    std::array<char, 64 * 1024> buffer{};
    for (const auto& file : collision_files) {
        hash_bytes(hash, file.data(), file.size());
        hash_bytes(hash, &separator, 1);

        const auto path = package_directory / file;
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error(
                "MapPackage collision payload not found: " + path.string());
        }
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            hash_bytes(hash, buffer.data(), static_cast<std::size_t>(input.gcount()));
        }
        if (!input.eof()) {
            throw std::runtime_error(
                "Failed to read MapPackage collision payload: " + path.string());
        }
        hash_bytes(hash, &separator, 1);
    }

    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

MapPackageManifest load_map_package_manifest(
    const std::filesystem::path& package_directory)
{
    const auto absolute_directory = std::filesystem::absolute(package_directory);
    const auto path = absolute_directory / "manifest.cfg";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "MapPackage manifest not found: " + path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::size_t line_number = 0;
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
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " must use key=value");
        }
        const auto key = trim(std::string_view(line).substr(0, separator));
        const auto value = trim(std::string_view(line).substr(separator + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " has an empty key or value");
        }
        if (!values.emplace(key, value).second) {
            throw std::runtime_error(
                "Duplicate MapPackage manifest key: " + key);
        }
    }

    const std::unordered_set<std::string> expected_keys{
        "format_version", "map_id", "coordinate_frame",
        "collision_files", "collision_checksum"};
    for (const auto& [key, value] : values) {
        if (!expected_keys.contains(key)) {
            throw std::runtime_error(
                "Unknown MapPackage manifest key: " + key);
        }
    }
    for (const auto& key : expected_keys) {
        if (!values.contains(key)) {
            throw std::runtime_error(
                "Missing MapPackage manifest key: " + key);
        }
    }
    if (values.at("format_version")
        != std::to_string(kMapPackageManifestFormatVersion)) {
        throw std::runtime_error(
            "Unsupported MapPackage manifest format_version: "
            + values.at("format_version"));
    }
    if (values.at("map_id").size() > 128) {
        throw std::runtime_error("MapPackage map_id exceeds 128 bytes");
    }
    if (values.at("coordinate_frame") != "map_enu") {
        throw std::runtime_error(
            "MapPackage coordinate_frame must be map_enu");
    }

    MapPackageManifest manifest;
    manifest.package_directory = absolute_directory;
    manifest.source_path = path;
    manifest.map_id = values.at("map_id");
    manifest.coordinate_frame = values.at("coordinate_frame");
    manifest.collision_files = parse_collision_files(
        values.at("collision_files"));
    manifest.collision_checksum = values.at("collision_checksum");
    const auto actual_checksum = compute_map_package_collision_checksum(
        absolute_directory, manifest.collision_files);
    if (manifest.collision_checksum != actual_checksum) {
        throw std::runtime_error(
            "MapPackage collision checksum mismatch: expected "
            + manifest.collision_checksum + ", computed " + actual_checksum);
    }
    return manifest;
}

} // namespace simcore_host
