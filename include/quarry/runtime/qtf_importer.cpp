#include "quarry/runtime/qtf_importer.hpp"
namespace quarry::runtime {
std::optional<std::vector<std::uint8_t>>
import_qtf(std::string_view text, const quarry::compiler::qbs::ValidatedQbsView& schema,
           const quarry::compiler::qbs::QbsRecordView& record,
           quarry::compiler::diagnostics::DiagnosticCollection& diagnostics,
           QtfParseLimits parse_limits, BrfEncodeLimits encode_limits) {
    const auto input = parse_qtf(text, schema, record, diagnostics, parse_limits);
    if (!input)
        return std::nullopt;
    GenericBrfEncodeError error_code = GenericBrfEncodeError::none;
    return encode_brf_record(schema, record, input->fields, &error_code, encode_limits);
}
} // namespace quarry::runtime
