#include "compiler/layout/layout.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quarry::compiler::layout {
namespace {

constexpr std::string_view layout_pass = "layout";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_too_many_fields(const semantic::SemanticRecord& record,
                          diagnostics::DiagnosticCollection& diagnostics,
                          context::CompilerContext& context) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC7001"), diagnostics::Severity::Error,
                                        "record '" + record.fqn + "' has more than 256 fields")
            .at(record.source_range)
            .from_pass(std::string(layout_pass))
            .build());
    (void)context;
}

void emit_duplicate_record_fqn(const semantic::SemanticRecord& record,
                               diagnostics::DiagnosticCollection& diagnostics,
                               context::CompilerContext& context) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC7002"), diagnostics::Severity::Error,
                         "record '" + record.fqn + "' is declared more than once in layout input")
                         .at(record.source_range)
                         .from_pass(std::string(layout_pass))
                         .build());
    (void)context;
}

} // namespace

const RecordLayout* LayoutModel::find_record(std::string_view fqn) const {
    const auto found =
        std::find_if(records.begin(), records.end(),
                     [fqn](const RecordLayout& record) { return record.fqn == fqn; });
    if (found == records.end()) {
        return nullptr;
    }
    return &*found;
}

LayoutModel LayoutComputer::compute(const semantic::SemanticModel& semantic_model,
                                    context::CompilerContext& context,
                                    diagnostics::DiagnosticCollection& diagnostics) const {
    LayoutModel layout_model;

    std::vector<const semantic::SemanticRecord*> ordered_records;
    ordered_records.reserve(semantic_model.records.size());
    for (const semantic::SemanticRecord& record : semantic_model.records) {
        ordered_records.push_back(&record);
    }

    std::stable_sort(ordered_records.begin(), ordered_records.end(),
                     [](const semantic::SemanticRecord* lhs, const semantic::SemanticRecord* rhs) {
                         return lhs->fqn < rhs->fqn;
                     });

    for (std::size_t index = 1; index < ordered_records.size(); ++index) {
        if (ordered_records[index - 1]->fqn == ordered_records[index]->fqn) {
            emit_duplicate_record_fqn(*ordered_records[index], diagnostics, context);
            return {};
        }
    }

    for (std::size_t index = 0; index < ordered_records.size(); ++index) {
        const semantic::SemanticRecord& record = *ordered_records[index];
        if (record.fields.size() > 256U) {
            emit_too_many_fields(record, diagnostics, context);
            return {};
        }

        RecordLayout record_layout;
        record_layout.fqn = record.fqn;
        record_layout.record_id = static_cast<std::uint32_t>(index + 1U);
        record_layout.fields.reserve(record.fields.size());
        for (std::size_t field_index = 0; field_index < record.fields.size(); ++field_index) {
            record_layout.fields.push_back(
                FieldLayout{.field_index = static_cast<std::uint32_t>(field_index)});
        }

        layout_model.records.push_back(std::move(record_layout));
    }

    return layout_model;
}

} // namespace quarry::compiler::layout
