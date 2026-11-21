#include "compiler/qbs/parser.hpp"
#include "quarry/runtime_c/generic_brf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace quarry::compiler;
    diagnostics::DiagnosticCollection diagnostics;
    qbs::QbsParserLimits limits;
    limits.max_image_size = 1U << 20U;
    limits.max_records = 64U;
    limits.max_fields = 256U;
    limits.max_types = 256U;
    limits.max_enums = 64U;
    limits.max_enum_values = 1024U;
    limits.max_strings = 1024U;
    limits.max_work_items = 4096U;
    limits.max_identity_key_bytes = 1U << 20U;
    const auto view = qbs::parse_qbs(std::span(data, size), diagnostics, limits);
    if (!view)
        return 0;
    const auto id = view->schema_id();
    if (id.algorithm != data[8] ||
        !std::equal(id.schema_id.begin(), id.schema_id.end(), data + 12U))
        std::abort();

    // Reuse the C parser only on structurally/canonically validated inputs.
    // Its caller-owned workspace is bounded independently of input counts.
    std::array<quarry_qbs_record_view_t, 64> records{};
    std::array<quarry_qbs_field_view_t, 256> fields{};
    std::array<quarry_qbs_type_view_t, 256> types{};
    std::array<quarry_qbs_enum_view_t, 64> enums{};
    std::array<std::uint64_t, 1024> values{};
    quarry_workspace_t workspace{};
    workspace.records = records.data();
    workspace.record_capacity = records.size();
    workspace.fields = fields.data();
    workspace.field_capacity = fields.size();
    workspace.types = types.data();
    workspace.type_capacity = types.size();
    workspace.enums = enums.data();
    workspace.enum_capacity = enums.size();
    workspace.enum_values = values.data();
    workspace.enum_value_capacity = values.size();
    quarry_qbs_view_t c_view{};
    if (quarry_qbs_parse(data, size, &c_view, &workspace, nullptr) == QUARRY_GENERIC_OK) {
        quarry_schema_id_t c_id{};
        if (quarry_qbs_schema_id(&c_view, &c_id) != QUARRY_GENERIC_OK ||
            !quarry_schema_id_equals(&c_id, id.algorithm, id.schema_id.data()))
            std::abort();
    }
    return 0;
}
