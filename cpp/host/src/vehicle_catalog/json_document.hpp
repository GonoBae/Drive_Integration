#pragma once

#include <google/protobuf/struct.pb.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace simcore_host::vehicle_catalog::json {

using Value = google::protobuf::Value;

// Retains JSON types and rejects duplicate keys, non-finite numbers, documents
// over 1 MiB, and nesting deeper than 32 containers. Errors include context.
Value parse(std::string_view bytes, const std::string& context);

// Reads at most 1 MiB; I/O and size errors include the source path.
std::string read_bytes(const std::filesystem::path& path);

}  // namespace simcore_host::vehicle_catalog::json
