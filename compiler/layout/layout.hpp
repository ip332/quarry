#pragma once

#include "quarry/schema_ir.pb.h"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/semantic/semantic.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::layout {

inline constexpr std::uint32_t kBrfV2HeaderSize = 16U;
inline constexpr std::uint32_t kBrfV2VariableDescriptorSize = 8U;

enum class RecordClassification {
    FixedSize,
    VariableSize,
};

enum class LayoutTypeKind {
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
    Enum,
    String,
    Bytes,
    Record,
    Array,
};

enum class FieldStorage {
    Fixed,
    InlineFixedNestedRecord,
    VariableDescriptor,
};

enum class DescriptorKind {
    None,
    DataOffsetByteLength,
};

struct FieldLocation {
    std::uint32_t byte_offset = 0U;
    std::uint8_t bit_offset = 0U;
    std::uint32_t bit_width = 0U;
};

struct TypeLayout {
    LayoutTypeKind kind = LayoutTypeKind::Bool;
    RecordClassification classification = RecordClassification::FixedSize;
    std::uint32_t encoded_width = 0U;
    std::uint32_t max_bytes = 0U;
    std::uint32_t max_elements = 0U;
    std::uint64_t referenced_ir_id = 0U;
    std::string referenced_fqn;
    std::vector<std::uint64_t> enum_values;
    std::unique_ptr<TypeLayout> element_type;

    TypeLayout() = default;
    TypeLayout(const TypeLayout& other);
    TypeLayout& operator=(const TypeLayout& other);
    TypeLayout(TypeLayout&&) noexcept = default;
    TypeLayout& operator=(TypeLayout&&) noexcept = default;
};

struct FieldLayout {
    std::uint32_t field_index = 0;
    std::uint32_t presence_bit_index = 0U;
    std::string name;
    TypeLayout type;
    FieldLocation location;
    std::uint32_t slot_size = 0U;
    FieldStorage storage = FieldStorage::Fixed;
    DescriptorKind descriptor_kind = DescriptorKind::None;
};

struct RecordLayout {
    std::string fqn;
    std::uint32_t record_id = 0;
    RecordClassification classification = RecordClassification::FixedSize;
    std::uint32_t header_size = kBrfV2HeaderSize;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::optional<std::uint32_t> complete_fixed_record_size;
    std::vector<FieldLayout> fields;
};

struct LayoutModel {
    std::vector<RecordLayout> records;

    [[nodiscard]] const RecordLayout* find_record(std::string_view fqn) const;
};

class LayoutComputer {
public:
    [[nodiscard]] LayoutModel compute(const semantic::SemanticModel& semantic_model,
                                      context::CompilerContext& context,
                                      diagnostics::DiagnosticCollection& diagnostics) const;

    [[nodiscard]] LayoutModel compute(const ::quarry::schema_ir::SchemaIR& schema_ir,
                                      diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace quarry::compiler::layout
