#include "vehicle_catalog/json_document.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace json = simcore_host::vehicle_catalog::json;
constexpr std::size_t byte_limit = 1024 * 1024;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

template<typename Action>
void expect_rejected(Action action, const std::string& context)
{
    try { action(); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()).starts_with(context + ": "),
                "JSON errors must retain their source context");
        return;
    }
    throw std::runtime_error(context + ": invalid JSON input was accepted");
}

void invalid_documents()
{
    const char* cases[]{
        "", " ", "{", "[}", "{} false", "[1,]", "{\"a\":1,}",
        "{'a':1}", "{a:1}", "/*comment*/{}", "NaN", "Infinity", "-Infinity",
        "1e999", "[1e999]", "01", "+1", "1.", "1e", "true false",
        "{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}",
        "{\"\":1,\"\":2}", "{\"a\":{\"b\":1,\"b\":2}}",
        "[{\"a\":1,\"a\":2}]", "\"\\q\"", "\"\\ud800\""
    };
    std::size_t index = 0;
    for (const auto bytes : cases) {
        const std::string context = "invalid fixture " + std::to_string(index++);
        expect_rejected([&] { (void)json::parse(bytes, context); }, context);
    }
    std::string invalid_utf8 = "\"";
    invalid_utf8.push_back(static_cast<char>(0xff));
    invalid_utf8 += '"';
    expect_rejected([&] { (void)json::parse(invalid_utf8, "UTF-8 fixture"); }, "UTF-8 fixture");
    const std::string embedded_nul("{}\0", 3);
    expect_rejected([&] { (void)json::parse(embedded_nul, "NUL fixture"); }, "NUL fixture");
}

void preserved_types()
{
    const auto value = json::parse(
        R"({"number":1,"string":"1","boolean":true,"null":null,"object":{},"array":[],"special":"NaN"})",
        "types fixture");
    require(value.kind_case() == json::Value::kStructValue, "root object kind must be preserved");
    const auto& fields = value.struct_value().fields();
    require(fields.at("number").kind_case() == json::Value::kNumberValue, "number coerced");
    require(fields.at("string").kind_case() == json::Value::kStringValue, "string coerced");
    require(fields.at("boolean").kind_case() == json::Value::kBoolValue, "boolean coerced");
    require(fields.at("null").kind_case() == json::Value::kNullValue, "null coerced");
    require(fields.at("object").kind_case() == json::Value::kStructValue, "empty object coerced");
    require(fields.at("array").kind_case() == json::Value::kListValue, "empty array coerced");
    require(fields.at("special").string_value() == "NaN", "non-numeric string must be retained");
    require(json::parse("false", "scalar").kind_case() == json::Value::kBoolValue,
            "top-level scalar kind must be preserved");
    const auto repeated = json::parse(R"([{"a":1},{"a":2}])", "independent objects");
    require(repeated.list_value().values_size() == 2, "same keys in independent objects are valid");
    const auto escaped = json::parse(R"({"text":"[\"{\\}]","":true})", "escaped string");
    require(escaped.struct_value().fields().at("").bool_value(), "empty object key is valid");
}

void document_bounds()
{
    const std::string depth32 = std::string(32, '[') + "0" + std::string(32, ']');
    require(json::parse(depth32, "depth 32").kind_case() == json::Value::kListValue,
            "32 nested containers must be supported");
    expect_rejected([&] { (void)json::parse("[" + depth32 + "]", "depth 33"); }, "depth 33");
    const std::string quoted_brackets = "\"" + std::string(200, '[') + "\"";
    require(json::parse(quoted_brackets, "quoted depth").string_value().size() == 200,
            "quoted brackets must not consume nesting depth");
    const std::string exact_limit = "\"" + std::string(byte_limit - 2, 'x') + "\"";
    require(json::parse(exact_limit, "exact limit").string_value().size() == byte_limit - 2,
            "exactly 1 MiB must be supported");
    expect_rejected([&] { (void)json::parse(exact_limit + " ", "oversized"); }, "oversized");
}

class TemporaryFile {
public:
    TemporaryFile()
        : path(std::filesystem::temp_directory_path() /
               ("simcore-catalog-json-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()) + ".json")) {}
    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    void write(const std::string& bytes) const
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(output.good(), "failed to write temporary JSON test fixture");
    }
    std::filesystem::path path;
};

void bounded_file_reads()
{
    TemporaryFile file;
    expect_rejected([&] { (void)json::read_bytes(file.path); }, file.path.string());
    file.write("");
    require(json::read_bytes(file.path).empty(), "empty file must read without altering its contents");
    const std::string exact_limit(byte_limit, ' ');
    file.write(exact_limit);
    require(json::read_bytes(file.path) == exact_limit, "exactly 1 MiB file must read unchanged");
    file.write(exact_limit + " ");
    expect_rejected([&] { (void)json::read_bytes(file.path); }, file.path.string());
    const std::string binary_bytes("{}\r\n\0", 5);
    file.write(binary_bytes);
    require(json::read_bytes(file.path) == binary_bytes, "reader must preserve binary bytes exactly");
}

} // namespace

int main()
{
    try {
        invalid_documents();
        preserved_types();
        document_bounds();
        bounded_file_reads();
        std::cout << "vehicle catalog JSON tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle catalog JSON test failure: " << error.what() << '\n';
        return 1;
    }
}
