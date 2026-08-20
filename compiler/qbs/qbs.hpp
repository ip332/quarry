#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "quarry/schema_ir.pb.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quarry::compiler::qbs {

inline constexpr std::uint8_t kQbsFormatVersion = 1U;
inline constexpr std::uint8_t kBrfFormatVersion = 2U;
inline constexpr std::uint16_t kQbsNoStringIndex = 0xFFFFU;
inline constexpr std::uint32_t kQbsMaxTableReference = 0xFFFFU;

enum class BuildMode {
    Minimal,
    Reflective,
};

enum class TypeCode : std::uint8_t {
    Bool = 1,
    I8 = 2,
    U8 = 3,
    I16 = 4,
    U16 = 5,
    I32 = 6,
    U32 = 7,
    I64 = 8,
    U64 = 9,
    F32 = 10,
    F64 = 11,
    Enum = 12,
    String = 13,
    Bytes = 14,
    Record = 15,
    Array = 16,
};

enum class Storage : std::uint8_t {
    Fixed = 0,
    InlineFixedNestedRecord = 1,
    VariableDescriptor = 2,
};

enum class DescriptorKind : std::uint8_t {
    None = 0,
    DataOffsetByteLength = 1,
};

struct QbsTypeModel {
    TypeCode code = TypeCode::Bool;
    bool fixed_size = true;
    std::uint32_t encoded_width = 0U;
    std::uint16_t reference = 0U;
    std::uint32_t max_elements = 0U;
    std::uint32_t max_bytes = 0U;

    friend bool operator==(const QbsTypeModel&, const QbsTypeModel&) = default;
};

struct QbsEnumModel {
    std::uint16_t table_index = 0U;
    std::string fqn;
    std::uint32_t encoded_width = 0U;
    std::uint32_t value_start = 0U;
    std::vector<std::uint64_t> values;
    std::uint16_t name_string_index = kQbsNoStringIndex;

    friend bool operator==(const QbsEnumModel&, const QbsEnumModel&) = default;
};

struct QbsFieldModel {
    std::uint16_t owning_record_index = 0U;
    std::uint16_t field_index = 0U;
    std::uint16_t type_index = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint16_t bit_offset = 0U;
    std::uint32_t bit_width = 0U;
    std::uint16_t presence_bit_index = 0U;
    std::uint32_t slot_size = 0U;
    Storage storage = Storage::Fixed;
    DescriptorKind descriptor_kind = DescriptorKind::None;
    std::uint16_t name_string_index = kQbsNoStringIndex;

    friend bool operator==(const QbsFieldModel&, const QbsFieldModel&) = default;
};

struct QbsRecordModel {
    std::uint16_t table_index = 0U;
    std::uint32_t record_id = 0U;
    std::uint32_t field_start = 0U;
    std::uint16_t field_count = 0U;
    bool variable_size = false;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::optional<std::uint32_t> complete_fixed_record_size;
    std::uint16_t name_string_index = kQbsNoStringIndex;
    std::string fqn;

    friend bool operator==(const QbsRecordModel&, const QbsRecordModel&) = default;
};

struct QbsImageModel {
    std::uint8_t format_version = kQbsFormatVersion;
    std::uint8_t brf_format_version = kBrfFormatVersion;
    std::uint8_t flags = 0U;
    BuildMode mode = BuildMode::Minimal;
    std::vector<QbsRecordModel> records;
    std::vector<QbsFieldModel> fields;
    std::vector<QbsTypeModel> types;
    std::vector<QbsEnumModel> enums;
    std::vector<std::uint64_t> enum_values;
    std::vector<std::string> strings;
    std::vector<std::uint8_t> schema_identity_input;

    friend bool operator==(const QbsImageModel&, const QbsImageModel&) = default;
};

struct QbsBuildOptions {
    BuildMode mode = BuildMode::Minimal;
};

class QbsModelBuilder {
public:
    [[nodiscard]] std::optional<QbsImageModel>
    build(const ::quarry::schema_ir::SchemaIR& schema_ir, const layout::LayoutModel& layout_model,
          QbsBuildOptions options, diagnostics::DiagnosticCollection& diagnostics) const;

    [[nodiscard]] bool validate(const QbsImageModel& model,
                                diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace quarry::compiler::qbs
