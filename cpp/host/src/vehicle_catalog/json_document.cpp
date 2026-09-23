#include "vehicle_catalog/json_document.hpp"

#include <google/protobuf/util/json_util.h>

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace simcore_host::vehicle_catalog::json {
namespace {

constexpr std::size_t max_document_bytes = 1024 * 1024;
constexpr std::size_t max_nesting = 32;

[[noreturn]] void fail(const std::string& context, const std::string& reason)
{
    throw std::runtime_error((context.empty() ? "JSON" : context) + ": " + reason);
}

// Bound parser recursion before building a document. String contents, including
// escaped quotes and brackets, do not contribute to container depth.
void check_bounds(std::string_view bytes, const std::string& context)
{
    if (bytes.size() > max_document_bytes) {
        fail(context, "JSON document exceeds 1 MiB");
    }
    std::array<char, max_nesting> containers{};
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const char c : bytes) {
        if (quoted) {
            if (escaped) { escaped = false; }
            else if (c == '\\') { escaped = true; }
            else if (c == '"') { quoted = false; }
            continue;
        }
        if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (depth == containers.size()) {
                fail(context, "JSON nesting exceeds 32 containers");
            }
            containers[depth++] = c;
        } else if (c == '}' || c == ']') {
            if (depth == 0 || containers[depth - 1] != (c == '}' ? '{' : '[')) {
                fail(context, "mismatched JSON container delimiter");
            }
            --depth;
        }
    }
    if (quoted || depth != 0) {
        fail(context, "unterminated JSON string or container");
    }
}

void check_finite_numbers(const Value& value, const std::string& context)
{
    switch (value.kind_case()) {
    case Value::kNumberValue:
        if (!std::isfinite(value.number_value())) {
            fail(context, "JSON numbers must be finite");
        }
        break;
    case Value::kStructValue:
        for (const auto& entry : value.struct_value().fields()) {
            check_finite_numbers(entry.second, context);
        }
        break;
    case Value::kListValue:
        for (const auto& item : value.list_value().values()) {
            check_finite_numbers(item, context);
        }
        break;
    default:
        break;
    }
}

}  // namespace

Value parse(std::string_view bytes, const std::string& context)
{
    check_bounds(bytes, context);
    google::protobuf::util::JsonParseOptions options;
    options.allow_legacy_nonconformant_behavior = false;
    Value value;
    // Protobuf's Struct map parser rejects repeated decoded object keys,
    // including nested objects and keys expressed with Unicode escapes.
    const auto status = google::protobuf::util::JsonStringToMessage(
        absl::string_view(bytes.data(), bytes.size()), &value, options);
    if (!status.ok()) {
        fail(context, "invalid JSON: " + status.ToString());
    }
    check_finite_numbers(value, context);
    return value;
}

std::string read_bytes(const std::filesystem::path& path)
{
    const std::string context = path.string();
    std::ifstream input(path, std::ios::binary);
    if (!input) { fail(context, "cannot open JSON document"); }

    // One extra byte detects growth or oversized files without an unbounded
    // allocation or a separate, potentially stale filesystem-size query.
    std::string bytes(max_document_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.bad() || (input.fail() && !input.eof())) {
        fail(context, "cannot read JSON document");
    }
    const auto count = static_cast<std::size_t>(input.gcount());
    if (count > max_document_bytes) {
        fail(context, "JSON document exceeds 1 MiB");
    }
    bytes.resize(count);
    return bytes;
}

}  // namespace simcore_host::vehicle_catalog::json
