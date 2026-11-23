// Source-tree feasibility consumer. These QBS/QTF APIs are not an installed SDK contract.
#include "compiler/qbs/parser.hpp"
#include "compiler/support/configuration_identity.hpp"
#include "dbc/exchange.generated.hpp"
#include "quarry/runtime/detail/brf_validation_cache.hpp"
#include "quarry/runtime/qtf_exporter.hpp"
#include "quarry/runtime/qtf_importer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace quarry::runtime;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::qbs::ValidatedQbsView;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string read(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "cannot open input");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Resolve fields through the generated QBS, never through DBC or hardcoded field indexes.
struct Record {
    const ValidatedQbsView& schema;
    const BrfRecordInput& input;
    const std::optional<BrfEncodeValue>& field(std::string_view name) const {
        for (std::size_t i = 0; i < schema.record_count(); ++i) {
            if (schema.record(i).record_id != input.record_id)
                continue;
            const auto field = schema.find_field_by_name(i, name);
            require(field.has_value(), "field missing from reflective QBS");
            return input.fields.at(field->field_index);
        }
        throw std::runtime_error("record missing from QBS");
    }
    std::uint64_t number(std::string_view name) const {
        return std::get<std::uint64_t>(field(name).value());
    }
    std::string_view text(std::string_view name) const {
        return std::get<std::string>(field(name).value());
    }
    bool boolean(std::string_view name) const { return std::get<bool>(field(name).value()); }
    BrfRecordArrayValue array(std::string_view name) const {
        return std::get<BrfRecordArrayValue>(field(name).value());
    }
    Record child(std::string_view name) const {
        return {schema, *std::get<BrfNestedRecordValue>(field(name).value())};
    }
};
void check_version(const Record& root) {
    require(root.number("model_version") == 1U, "unsupported model version");
}
std::string replace(std::string input, std::string_view from, std::string_view to) {
    const auto at = input.find(from);
    require(at != std::string::npos, "probe replacement not found");
    input.replace(at, from.size(), to);
    return input;
}
// Isolate the cache operation order used by the recursive reader. No DBC,
// QTF, BRD, encoder, field lookups or consumer-selected indexes are involved.
void investigate_cache() {
    detail::ValidationCache cache;
    const auto root = cache.add_node(0U, 0U, 1U).value();
    const auto outer = cache.begin_array(root, 0U, 1U).value();
    const auto frame = cache.add_node(1U, 1U, 1U).value();
    const auto inner = cache.begin_array(frame, 0U, 1U).value();
    const auto enumeration = cache.add_node(2U, 2U, 1U).value();
    require(cache.add_array_element(inner, enumeration), "inner cache append failed");
    require(cache.add_array_element(outer, frame), "outer cache append failed");
    const auto observed = cache.array_children(outer).at(0U);
    require(observed == frame, "nested cache child order changed");
    std::cout << "Cache isolation: outer expected node " << frame << ", observed " << observed
              << "; inner node " << enumeration << '\n';
}

void study(const ValidatedQbsView& schema, const std::string& text) {
    const auto root_schema = schema.find_record_by_identity("dbc.exchange.DbcModel");
    require(root_schema.has_value(), "model record missing");
    DiagnosticCollection diagnostics;
    require(!parse_qtf(text, schema, *root_schema, diagnostics), "default v1 accepted comments");
    diagnostics.clear();
    const auto input =
        parse_qtf(text, schema, *root_schema, diagnostics, {.allow_line_comments = true});
    if (!input) {
        for (const auto& error : diagnostics.diagnostics())
            std::cerr << error.message() << '\n';
        throw std::runtime_error("QTF parse failed");
    }
    const Record root{schema, *input};
    check_version(root);
    require(root.text("exporter_version") == "hand-authored-study", "exporter metadata changed");
    const auto sources = root.array("sources"), messages = root.array("messages");
    require(sources->size() == 1U && messages->size() == 1U, "collection size changed");
    const Record source{schema, *sources->at(0)}, message{schema, *messages->at(0)};
    require(source.text("name") == "source.dbc" && source.text("sha256").size() == 64U,
            "source provenance lost");
    require(message.text("name") == "Example" && message.number("dbc_id") == 100U &&
                message.number("dlc") == 8U && message.number("source_line") == 1U,
            "message identity/provenance changed");
    require(!message.field("description"), "absent description became present");
    const auto signals = message.array("signals");
    require(signals->size() == 3U, "signal count changed");
    const Record selector{schema, *signals->at(0)}, intel{schema, *signals->at(1)},
        motorola{schema, *signals->at(2)};
    require(!selector.field("unit"), "absent unit became present");
    require(motorola.field("unit") && motorola.text("unit").empty(), "present empty unit lost");
    require(intel.number("spn") == 0U && !motorola.field("spn"), "SPN presence lost");
    require(intel.number("start_bit") == 8U && intel.number("width") == 12U &&
                intel.number("byte_order") == 0U && intel.boolean("is_signed"),
            "Intel layout changed");
    require(motorola.number("start_bit") == 23U && motorola.number("width") == 12U &&
                motorola.number("byte_order") == 1U && motorola.boolean("is_signed"),
            "Motorola layout changed");
    require(intel.text("scale") == "0.123456789012345678901234567890" &&
                intel.text("offset") == "-2.000000000000000000000000000001",
            "decimal precision lost");
    require(selector.boolean("is_mux_switch") && intel.text("mux_switch") == "Selector" &&
                intel.number("mux_value") == 0U && motorola.text("mux_switch") == "Selector" &&
                motorola.number("mux_value") == 1U,
            "mux metadata changed");
    const auto values = selector.array("enums");
    require(values->size() == 2U && Record{schema, *values->at(1)}.text("label") == "Motorola mode",
            "enum collection changed");
    const auto probe = root.child("u64_probe");
    require(probe.number("raw") == std::numeric_limits<std::uint64_t>::max(),
            "uint64 precision lost");
    require(probe.text("label") == "literal # stays data", "hash inside string was a comment");
    for (const auto& invalid :
         {replace(text, "model_version: 1", "model_version: 1 model_version: 1"),
          replace(text, "model_version: 1", "unknown: 1 model_version: 1"),
          replace(text, "18446744073709551615", "18446744073709551616"),
          replace(text, "unit: \"rpm\"", "unit: \"123456789012345678901234567890123\""),
          replace(text, "byte_order: 0", "byte_order: 2"),
          replace(text, "messages: [", "messages: [{}, {}, ")}) {
        diagnostics.clear();
        require(
            !parse_qtf(invalid, schema, *root_schema, diagnostics, {.allow_line_comments = true}),
            "invalid input accepted");
    }
    diagnostics.clear();
    const auto future = parse_qtf(replace(text, "model_version: 1", "model_version: 2"), schema,
                                  *root_schema, diagnostics, {.allow_line_comments = true});
    require(future.has_value(),
            "u32 model-version field should accept the value before model policy");
    bool rejected = false;
    try {
        check_version({schema, *future});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "consumer failed to reject unknown model version");
    std::cout
        << "PASS: BRD -> QBS; QTF + QBS -> independent host model consumer (no DBC parsing)\n";

    // Require the formerly failing BRF/canonical-export route, independently of parsed QTF.
    diagnostics.clear();
    const auto bytes =
        import_qtf(text, schema, *root_schema, diagnostics, {.allow_line_comments = true});
    require(bytes.has_value(), "BRF import failed");
    // The generated owning decoder is independent of ValidatedBrfRecordView's cache.
    const auto generated = ::dbc::exchange::decode_DbcModel(std::as_bytes(std::span(*bytes)));
    require(generated && generated->messages() && generated->messages()->size() == 1U,
            "independent generated decoder rejected the same BRF bytes");
    const auto& generated_frame = generated->messages()->at(0U);
    require(generated_frame.name() && *generated_frame.name() == "Example" &&
                generated_frame.signals() && generated_frame.signals()->size() == 3U,
            "independent decoder saw a different FrameDefinition");
    const auto& generated_intel = generated_frame.signals()->at(1U);
    require(generated_intel.name() && *generated_intel.name() == "Intel" &&
                generated_intel.scale() &&
                *generated_intel.scale() == "0.123456789012345678901234567890" &&
                generated->u64_probe() && generated->u64_probe()->raw() &&
                *generated->u64_probe()->raw() == std::numeric_limits<std::uint64_t>::max(),
            "independent decoder lost nested/uint64/decimal data");
    const auto generated_bytes = ::dbc::exchange::encode(*generated);
    const auto imported_bytes = std::as_bytes(std::span(*bytes));
    require(generated_bytes && std::equal(generated_bytes->begin(), generated_bytes->end(),
                                          imported_bytes.begin(), imported_bytes.end()),
            "generated owning decode/re-encode changed the imported bytes");
    std::cout << "PASS: generated owning decoder reads the same imported BRF correctly\n";
    investigate_cache();
    const auto view = validate_brf_record(schema, *root_schema, *bytes);
    require(view.has_value(), "BRF validation failed");
    const auto root_index = [&]() {
        for (std::size_t i = 0; i < schema.record_count(); ++i)
            if (schema.record(i).record_id == root_schema->record_id)
                return i;
        throw std::runtime_error("root index not found");
    }();
    const auto messages_field = schema.find_field_by_name(root_index, "messages");
    require(messages_field.has_value(), "messages field missing");
    const auto message_array = view->record_array(messages_field->field_index);
    require(message_array && message_array->size() == 1U, "BRF message array missing");
    const auto first = message_array->element(0U);
    require(first.has_value(), "BRF message missing");
    std::cout << "BRF bytes: " << bytes->size() << '\n';
    require(first->schema().identity == "dbc.exchange.FrameDefinition",
            "nested record-array lookup returned the wrong record identity");
    const auto canonical = export_qtf(*view);
    require(canonical.text.has_value(), "canonical export failed");
    diagnostics.clear();
    require(import_qtf(*canonical.text, schema, *root_schema, diagnostics) == bytes,
            "round trip changed values");
    std::cout << "PASS: BRF/canonical-QTF round trip\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--require-roundtrip"),
                "usage: consumer model.qbs model.qtf expected-schema-id.txt [--require-roundtrip]");
        const auto qbs = read(argv[1]);
        const std::vector<std::uint8_t> image(qbs.begin(), qbs.end());
        DiagnosticCollection diagnostics;
        const auto schema = quarry::compiler::qbs::parse_qbs(image, diagnostics);
        require(schema.has_value(), "QBS parse failed");
        auto expected = read(argv[3]);
        if (!expected.empty() && expected.back() == '\n')
            expected.pop_back();
        require(schema->schema_id().algorithm == 1U &&
                    quarry::compiler::support::schema_id_hex(schema->schema_id()) == expected,
                "QBS identity mismatch");
        study(*schema, read(argv[2]));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
