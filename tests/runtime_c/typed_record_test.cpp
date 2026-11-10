#include <quarry/runtime_c/typed_record.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

#define SCALARS(X)                                                                                 \
    X(boolean, bool, BOOL, 1, 17)                                                                  \
    X(i8, int8_t, I8, 1, 18)                                                                       \
    X(u8, uint8_t, U8, 1, 19)                                                                      \
    X(i16, int16_t, I16, 2, 20)                                                                    \
    X(u16, uint16_t, U16, 2, 22)                                                                   \
    X(i32, int32_t, I32, 4, 24)                                                                    \
    X(u32, uint32_t, U32, 4, 28)                                                                   \
    X(i64, int64_t, I64, 8, 32)                                                                    \
    X(u64, uint64_t, U64, 8, 40)                                                                   \
    X(f32, float, F32, 4, 48)                                                                      \
    X(f64, double, F64, 8, 52)

struct Scalars {
#define MEMBER(N, T, K, W, O)                                                                      \
    bool has_##N;                                                                                  \
    T N;
    SCALARS(MEMBER)
#undef MEMBER
};

const quarry_c_brf_v2_field_layout_t fields[] = {
#define FIELD(N, T, K, W, O) {O, 0U, O, 0U, W * 8U, W, QUARRY_C_BRF_V2_STORAGE_FIXED},
    SCALARS(FIELD)
#undef FIELD
};
// Use dense presence bits, while retaining independent schema field indexes.
const auto wire_fields = [] {
    std::array<quarry_c_brf_v2_field_layout_t, 11> result{};
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = fields[i];
        result[i].presence_bit_index = static_cast<uint32_t>(i);
        ++result[i].byte_offset; // Two bitmap bytes, first scalar begins at 18.
    }
    return result;
}();
const quarry_c_brf_v2_record_layout_t layout = {42U, 2U, 45U, 11U, wire_fields.data()};
const quarry_c_member_descriptor_t members[] = {
#define BIND(N, T, K, W, O)                                                                        \
    {offsetof(Scalars, has_##N),                                                                   \
     offsetof(Scalars, N),                                                                         \
     0U,                                                                                           \
     0U,                                                                                           \
     {QUARRY_C_TYPED_##K, sizeof(T), 0U, 0U, W, nullptr, 0U, false, nullptr, 0U, nullptr,          \
      nullptr}},
    SCALARS(BIND)
#undef BIND
};
const quarry_c_type_descriptor_t type = {&layout, sizeof(Scalars), members, nullptr};
#undef SCALARS

TEST(TypedRecordTest, PackedPresenceCrossesByteBoundaryAndPreservesScalarZero) {
    struct Packed { uint8_t bits[2]; Scalars values; };
    std::array<quarry_c_member_descriptor_t, 11> bindings{};
    for (size_t i = 0U; i < bindings.size(); ++i) {
        bindings[i] = members[i];
        bindings[i].presence_offset = QUARRY_C_PACKED_PRESENCE(offsetof(Packed, bits), i);
        bindings[i].value_offset += offsetof(Packed, values);
    }
    const quarry_c_type_descriptor_t packed_type = {&layout, sizeof(Packed), bindings.data(),
                                                    nullptr};
    Packed value{};
    value.bits[0] = 1U; // Present false boolean.
    value.bits[1] = 4U; // Field 10, in the second byte.
    value.values.f64 = 3.5;
    std::array<uint8_t, 128> bytes{};
    const auto encoded = quarry_c_typed_encode(&packed_type, &value, 42U, bytes.data(), bytes.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(bytes[16], 1U);
    EXPECT_EQ(bytes[17], 4U);
    Packed decoded;
    std::memset(&decoded, 0xff, sizeof(decoded));
    const auto result = quarry_c_typed_decode(&packed_type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(decoded.bits[0], 1U);
    EXPECT_EQ(decoded.bits[1], 4U);
    EXPECT_FALSE(decoded.values.boolean);
    EXPECT_DOUBLE_EQ(decoded.values.f64, 3.5);
    EXPECT_EQ(quarry_c_typed_encoded_size(&packed_type, &value).size, encoded.bytes_written);
}

TEST(TypedRecordTest, ScalarExtremesRoundTripWithoutHostLayoutAssumptions) {
    Scalars value{};
    value.has_boolean = value.has_i8 = value.has_u8 = value.has_i16 = value.has_u16 = true;
    value.has_i32 = value.has_u32 = value.has_i64 = value.has_u64 = true;
    value.has_f32 = value.has_f64 = true;
    value.boolean = true;
    value.i8 = INT8_MIN;
    value.u8 = UINT8_MAX;
    value.i16 = INT16_MIN;
    value.u16 = UINT16_MAX;
    value.i32 = INT32_MIN;
    value.u32 = UINT32_MAX;
    value.i64 = INT64_MIN;
    value.u64 = UINT64_MAX;
    value.f32 = -1.25F;
    value.f64 = std::numeric_limits<double>::infinity();
    std::array<uint8_t, 128> bytes{};
    auto encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), bytes.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(encoded.bytes_written, 61U);
    const auto measured = quarry_c_typed_encoded_size(&type, &value);
    ASSERT_EQ(measured.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(measured.size, encoded.bytes_written);
    EXPECT_EQ(bytes[18], 1U);
    EXPECT_EQ(bytes[19], 0x80U);
    EXPECT_EQ(bytes[33], 0x80U);
    Scalars decoded{};
    auto result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
    EXPECT_FALSE(result.has_byte_offset);
    EXPECT_EQ(decoded.i8, value.i8);
    EXPECT_EQ(decoded.u8, value.u8);
    EXPECT_EQ(decoded.i16, value.i16);
    EXPECT_EQ(decoded.u16, value.u16);
    EXPECT_EQ(decoded.i32, value.i32);
    EXPECT_EQ(decoded.u32, value.u32);
    EXPECT_EQ(decoded.i64, value.i64);
    EXPECT_EQ(decoded.u64, value.u64);
    EXPECT_EQ(decoded.f32, value.f32);
    EXPECT_EQ(decoded.f64, value.f64);
    EXPECT_TRUE(decoded.has_boolean);
    EXPECT_TRUE(decoded.boolean);

    bytes[18] = 2U;
    result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_BOOL);
    EXPECT_TRUE(result.has_byte_offset);
    EXPECT_EQ(result.byte_offset, 18U);
}

TEST(TypedRecordTest, ShortHeaderCapacityDoesNotWriteAndAbsentValuesStayZero) {
    Scalars value{};
    std::array<uint8_t, 128> bytes;
    bytes.fill(0xA5U);
    const auto before = bytes;
    auto encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), 16U);
    EXPECT_EQ(encoded.status, QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);
    EXPECT_EQ(encoded.bytes_written, 0U);
    EXPECT_EQ(bytes, before);
    encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), bytes.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    Scalars decoded;
    std::memset(&decoded, 0xA5, sizeof(decoded));
    auto result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
    EXPECT_FALSE(decoded.has_i64);
    EXPECT_EQ(decoded.i64, 0);
    EXPECT_EQ(decoded.f64, 0.0);
    bytes[19] = 1U;
    result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_FIELD_RANGE);
    EXPECT_EQ(result.byte_offset, 19U);
}

TEST(TypedRecordTest, OrdinalAccessMatchesSparseIndexLookup) {
    Scalars value{};
    value.has_u64 = true;
    value.u64 = 123U;
    std::array<uint8_t, 128> bytes{};
    const auto encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), bytes.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    quarry_c_brf_v2_parsed_record_t parsed{};
    size_t error = 0U;
    ASSERT_EQ(
        quarry_c_brf_v2_parse_record(bytes.data(), encoded.bytes_written, &layout, &parsed, &error),
        QUARRY_C_STATUS_OK);
    for (uint32_t i = 0U; i < layout.field_count; ++i) {
        quarry_c_brf_v2_field_view_t indexed{}, searched{};
        ASSERT_EQ(quarry_c_brf_v2_field_at(&parsed, &layout, i, &indexed), QUARRY_C_STATUS_OK);
        ASSERT_EQ(
            quarry_c_brf_v2_find_field(&parsed, &layout, layout.fields[i].field_index, &searched),
            QUARRY_C_STATUS_OK);
        EXPECT_EQ(indexed.present, searched.present);
        EXPECT_EQ(indexed.bytes, searched.bytes);
        EXPECT_EQ(indexed.length, searched.length);
        EXPECT_EQ(indexed.byte_offset, searched.byte_offset);
    }
    quarry_c_brf_v2_field_view_t view{};
    EXPECT_EQ(quarry_c_brf_v2_field_at(&parsed, &layout, layout.field_count, &view),
              QUARRY_C_STATUS_INVALID_FIELD_RANGE);
    EXPECT_EQ(quarry_c_brf_v2_field_at(nullptr, &layout, 0U, &view),
              QUARRY_C_STATUS_INVALID_ARGUMENT);
}

TEST(TypedRecordTest, NullArgumentsFailWithoutDereferencing) {
    Scalars value{};
    const auto null_value = quarry_c_typed_encoded_size(&type, nullptr);
    EXPECT_EQ(null_value.status, QUARRY_C_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(null_value.size, 0U);
    const auto null_type = quarry_c_typed_encoded_size(nullptr, &value);
    EXPECT_EQ(null_type.status, QUARRY_C_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(null_type.size, 0U);
    EXPECT_EQ(quarry_c_typed_encode(&type, &value, 42U, nullptr, 0U).status,
              QUARRY_C_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(quarry_c_typed_decode(&type, nullptr, 0U, 42U, &value).status,
              QUARRY_C_STATUS_INVALID_HEADER);
    EXPECT_EQ(quarry_c_typed_decode(&type, nullptr, 0U, 42U, nullptr).status,
              QUARRY_C_STATUS_INVALID_ARGUMENT);
}
} // namespace

namespace {
struct VariableFields {
    char text[5];
    uint32_t text_length;
    uint8_t bytes[4];
    uint32_t bytes_length;
    uint8_t items[4];
    uint32_t items_count;
};
const quarry_c_brf_v2_field_layout_t variable_fields[] = {
    {0U, 0U, 17U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
    {1U, 1U, 25U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
    {2U, 2U, 33U, 0U, 8U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
};
const quarry_c_brf_v2_record_layout_t variable_layout = {9U, 1U, 25U, 3U, variable_fields};
const quarry_c_member_descriptor_t variable_members[] = {
    {QUARRY_C_COUNT_PRESENCE(offsetof(VariableFields, text_length)),
     offsetof(VariableFields, text),
     0U,
     0U,
     {QUARRY_C_TYPED_STRING, 5U,
      offsetof(VariableFields, text_length) - offsetof(VariableFields, text), 4U, 0U, nullptr, 0U,
      false, nullptr, 0U, nullptr, nullptr}},
    {QUARRY_C_COUNT_PRESENCE(offsetof(VariableFields, bytes_length)),
     offsetof(VariableFields, bytes),
     0U,
     0U,
     {QUARRY_C_TYPED_BYTES, 4U,
      offsetof(VariableFields, bytes_length) - offsetof(VariableFields, bytes), 4U, 0U, nullptr, 0U,
      false, nullptr, 0U, nullptr, nullptr}},
    {QUARRY_C_COUNT_PRESENCE(offsetof(VariableFields, items_count)),
     offsetof(VariableFields, items),
     offsetof(VariableFields, items_count),
     4U,
     {QUARRY_C_TYPED_U8, 1U, 0U, 0U, 1U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}},
};
const quarry_c_type_descriptor_t variable_type = {&variable_layout, sizeof(VariableFields),
                                                  variable_members, nullptr};

TEST(TypedRecordTest, LengthAndCountDeterminePresenceWithoutFlags) {
    VariableFields value{};
    std::memset(value.text, 0xFF, sizeof(value.text)); // Ignored when length is zero.
    std::array<uint8_t, 64> output{};
    auto measured = quarry_c_typed_encoded_size(&variable_type, &value);
    ASSERT_EQ(measured.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(measured.size, 41U);
    auto encoded = quarry_c_typed_encode(&variable_type, &value, 9U, output.data(), output.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(encoded.bytes_written, measured.size);
    EXPECT_EQ(output[16U], 0U);
    for (size_t i = 17U; i < encoded.bytes_written; ++i) EXPECT_EQ(output[i], 0U);

    value.text[0] = '\0'; // A one-byte NUL string is nonempty.
    value.text_length = value.bytes_length = value.items_count = 1U;
    encoded = quarry_c_typed_encode(&variable_type, &value, 9U, output.data(), output.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(encoded.bytes_written, 45U);
    EXPECT_EQ(output[16U], 7U);
    VariableFields decoded{};
    auto result = quarry_c_typed_decode(&variable_type, output.data(), encoded.bytes_written, 9U, &decoded);
    ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(decoded.text_length, 1U);
    EXPECT_EQ(decoded.bytes_length, 1U);
    EXPECT_EQ(decoded.items_count, 1U);
    EXPECT_EQ(decoded.text[0], '\0');
    EXPECT_EQ(decoded.bytes[0], 0U);
    EXPECT_EQ(decoded.items[0], 0U);
}

TEST(TypedRecordTest, VariablePayloadOverwritesDirtyOutputAndKeepsCapacityTailUntouched) {
    VariableFields value{};
    std::memcpy(value.text, "text", 4U);
    value.text_length = value.bytes_length = value.items_count = 4U;
    for (size_t i = 0U; i < 4U; ++i) {
        value.bytes[i] = static_cast<uint8_t>(i);
        value.items[i] = static_cast<uint8_t>(i + 10U);
    }
    std::array<uint8_t, 64> dirty{}, clean{};
    dirty.fill(0xA5U);
    const auto encoded =
        quarry_c_typed_encode(&variable_type, &value, 9U, dirty.data(), dirty.size());
    const auto reference =
        quarry_c_typed_encode(&variable_type, &value, 9U, clean.data(), clean.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    ASSERT_EQ(reference.status, QUARRY_C_STATUS_OK);
    ASSERT_EQ(encoded.bytes_written, reference.bytes_written);
    for (size_t i = 0U; i < dirty.size(); ++i)
        EXPECT_EQ(dirty[i], i < encoded.bytes_written ? clean[i] : 0xA5U);
    value.text[0] = static_cast<char>(0xC0);
    value.text[1] = static_cast<char>(0x80);
    EXPECT_EQ(quarry_c_typed_encode(&variable_type, &value, 9U, dirty.data(), dirty.size()).status,
              QUARRY_C_STATUS_INVALID_UTF8);
    // Output may now contain an incomplete record; callers discard it.
}

TEST(TypedRecordTest, LegacyPresentEmptyFieldsRemainDecodableAndReencodeAsAbsent) {
    std::array<uint8_t, 64> legacy{};
    VariableFields value{};
    ASSERT_EQ(quarry_c_typed_encode(&variable_type, &value, 9U, legacy.data(), legacy.size()).status,
              QUARRY_C_STATUS_OK);
    legacy[16U] = 7U;
    quarry_c_brf_v2_write_u32(legacy.data() + 12U, 42U);
    quarry_c_brf_v2_write_u32(legacy.data() + 17U, 41U);
    quarry_c_brf_v2_write_u32(legacy.data() + 25U, 41U);
    quarry_c_brf_v2_write_u32(legacy.data() + 33U, 41U);
    quarry_c_brf_v2_write_u32(legacy.data() + 37U, 1U);
    auto result = quarry_c_typed_decode(&variable_type, legacy.data(), 42U, 9U, &value);
    ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(value.text_length, 0U);
    EXPECT_EQ(value.bytes_length, 0U);
    EXPECT_EQ(value.items_count, 0U);
    std::array<uint8_t, 64> normalized{};
    auto encoded = quarry_c_typed_encode(&variable_type, &value, 9U, normalized.data(), normalized.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    EXPECT_EQ(encoded.bytes_written, 41U);
    EXPECT_EQ(normalized[16U], 0U);
}
} // namespace

namespace {
TEST(TypedRecordTest, WideRecordsPreserveWireAndReportCapacityErrors) {
    // Large records must work without any per-field encoding workspace.
    for (const uint32_t count : {64U, 65U, 130U}) {
        std::array<quarry_c_brf_v2_field_layout_t, 130> slots{};
        std::array<quarry_c_member_descriptor_t, 130> bindings{};
        const uint32_t bitmap = (count + 7U) / 8U;
        for (uint32_t i = 0U; i < count; ++i) {
            slots[i] = {i, i, 16U + bitmap + i, 0U, 8U, 1U, QUARRY_C_BRF_V2_STORAGE_FIXED};
            bindings[i] = members[0];
        }
        const quarry_c_brf_v2_record_layout_t wire = {42U, bitmap, bitmap + count, count,
                                                      slots.data()};
        const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Scalars), bindings.data(),
                                                       nullptr};
        Scalars value{};
        value.has_boolean = true;
        value.boolean = true;
        std::array<uint8_t, 512> output{};
        const auto encoded =
            quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), output.size());
        ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
        EXPECT_EQ(encoded.bytes_written, 16U + bitmap + count);
        for (uint32_t i = 0U; i < count; ++i) {
            EXPECT_TRUE(quarry_c_brf_v2_present_bit(output.data(), i));
            EXPECT_EQ(output[16U + bitmap + i], 1U);
        }
        Scalars decoded{};
        ASSERT_EQ(
            quarry_c_typed_decode(&descriptor, output.data(), encoded.bytes_written, 42U, &decoded)
                .status,
            QUARRY_C_STATUS_OK);
        EXPECT_TRUE(decoded.has_boolean);
        EXPECT_TRUE(decoded.boolean);
        const auto before = output;
        EXPECT_EQ(quarry_c_typed_encode(&descriptor, &value, 42U, output.data(),
                                        encoded.bytes_written - 1U)
                      .status,
                  QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);
        EXPECT_EQ(output.back(), before.back()); // Never write beyond capacity.
        // Native storage may change between calls: no stale presence cache.
        value.has_boolean = false;
        ASSERT_EQ(
            quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), output.size()).status,
            QUARRY_C_STATUS_OK);
        for (size_t i = 16U; i < encoded.bytes_written; ++i)
            EXPECT_EQ(output[i], 0U);
    }
}
} // namespace

namespace {
TEST(TypedRecordTest, EnumValidationFailureRequiresDiscardingOutput) {
    const uint64_t allowed[] = {0U, 2U};
    auto binding = members[6]; // Scalars::u32, with ordinary presence storage.
    binding.element.kind = QUARRY_C_TYPED_ENUM;
    binding.element.wire_width = 1U;
    binding.element.enum_values = allowed;
    binding.element.enum_count = 2U;
    binding.element.enum_load = [](const void* p) -> uint64_t {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    };
    binding.element.enum_store = [](void* p, uint64_t v) {
        const auto native = static_cast<uint32_t>(v);
        std::memcpy(p, &native, sizeof(native));
    };
    const quarry_c_brf_v2_field_layout_t slot = {
        0U, 0U, 17U, 0U, 8U, 1U, QUARRY_C_BRF_V2_STORAGE_FIXED};
    const quarry_c_brf_v2_record_layout_t wire = {42U, 1U, 2U, 1U, &slot};
    const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Scalars), &binding, nullptr};
    Scalars value{};
    value.has_u32 = true;
    value.u32 = 1U; // Representable on the wire, but not declared by the schema.
    std::array<uint8_t, 32> output{};
    output.fill(0xA5U);
    const auto before = output;
    EXPECT_EQ(quarry_c_typed_encoded_size(&descriptor, &value).status,
              QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE);
    for (const size_t capacity : {size_t{0}, output.size()}) {
        const auto result =
            quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), capacity);
        EXPECT_EQ(result.status, capacity == 0U ? QUARRY_C_STATUS_INSUFFICIENT_CAPACITY
                                                : QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE);
        EXPECT_EQ(result.bytes_written, capacity == 0U ? 0U : 18U);
        EXPECT_EQ(output.back(), before.back());
        if (capacity == output.size())
            EXPECT_NE(output, before); // The header was written before the enum failed.
    }
    value.u32 = 2U;
    ASSERT_EQ(quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), output.size()).status,
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(output[17U], 2U);
    Scalars decoded{};
    ASSERT_EQ(quarry_c_typed_decode(&descriptor, output.data(), 18U, 42U, &decoded).status,
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(decoded.u32, 2U);
    value.u32 = 1U;
    value.has_u32 = false; // An invalid value in an absent member is ignored.
    ASSERT_EQ(quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), output.size()).status,
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(output[16U], 0U);
    EXPECT_EQ(output[17U], 0U);
}
} // namespace

namespace {
TEST(TypedRecordTest, OnePassPrefixedArraysAcceptExactCapacityAndStayWithinBounds) {
    struct Text {
        char data[256];
        uint32_t length;
    };
    struct Array {
        Text values[2];
        uint32_t count;
    };
    const quarry_c_brf_v2_field_layout_t slot = {
        0U, 0U, 17U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR};
    const quarry_c_brf_v2_record_layout_t child_wire = {43U, 1U, 9U, 1U, &slot};
    quarry_c_member_descriptor_t child_member{};
    child_member.presence_offset = QUARRY_C_COUNT_PRESENCE(offsetof(Text, length));
    child_member.element.kind = QUARRY_C_TYPED_STRING;
    child_member.element.size = sizeof(Text);
    child_member.element.length_offset = offsetof(Text, length);
    child_member.element.bound = 256U;
    const quarry_c_type_descriptor_t child = {&child_wire, sizeof(Text), &child_member, nullptr};
    const quarry_c_brf_v2_record_layout_t root_wire = {42U, 1U, 9U, 1U, &slot};
    for (const bool nested : {false, true}) {
        auto member = child_member;
        member.presence_offset = QUARRY_C_COUNT_PRESENCE(offsetof(Array, count));
        member.count_offset = offsetof(Array, count);
        member.max_count = 2U;
        if (nested) {
            member.element.kind = QUARRY_C_TYPED_RECORD;
            member.element.record = &child;
            member.element.record_id = 43U;
        }
        const quarry_c_type_descriptor_t descriptor = {&root_wire, sizeof(Array), &member, nullptr};
        Array value{};
        value.count = 2U;
        value.values[0].length = 127U;
        value.values[1].length = 128U;
        std::memset(value.values[0].data, 'a', 127U);
        std::memset(value.values[1].data, 'b', 128U);
        const auto measured = quarry_c_typed_encoded_size(&descriptor, &value);
        ASSERT_EQ(measured.status, QUARRY_C_STATUS_OK);
        std::array<uint8_t, 512> bytes{};
        for (size_t capacity = 0U; capacity <= measured.size; ++capacity) {
            bytes.fill(0xA5U);
            const auto encoded =
                quarry_c_typed_encode(&descriptor, &value, 42U, bytes.data(), capacity);
            EXPECT_EQ(encoded.status, capacity == measured.size
                                          ? QUARRY_C_STATUS_OK
                                          : QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);
            EXPECT_LE(encoded.bytes_written, capacity);
            if (capacity == measured.size)
                EXPECT_EQ(encoded.bytes_written, measured.size);
            for (size_t i = encoded.bytes_written; i < bytes.size(); ++i)
                ASSERT_EQ(bytes[i], 0xA5U);
            for (size_t i = capacity; i < bytes.size(); ++i)
                ASSERT_EQ(bytes[i], 0xA5U);
        }
        Array decoded{};
        ASSERT_EQ(
            quarry_c_typed_decode(&descriptor, bytes.data(), measured.size, 42U, &decoded).status,
            QUARRY_C_STATUS_OK);
        EXPECT_EQ(decoded.count, 2U);
        for (size_t i = 0U; i < 2U; ++i) {
            EXPECT_EQ(decoded.values[i].length, value.values[i].length);
            EXPECT_EQ(
                std::memcmp(decoded.values[i].data, value.values[i].data, value.values[i].length),
                0);
        }
        // A late invalid element must report writes inside the failing child.
        bytes.fill(0xA5U);
        value.values[1].data[0] = static_cast<char>(0xC0);
        const auto failed =
            quarry_c_typed_encode(&descriptor, &value, 42U, bytes.data(), bytes.size());
        EXPECT_EQ(failed.status, QUARRY_C_STATUS_INVALID_UTF8);
        EXPECT_EQ(failed.bytes_written, nested ? 206U : 155U);
        for (size_t i = failed.bytes_written; i < bytes.size(); ++i)
            ASSERT_EQ(bytes[i], 0xA5U);
    }
}
} // namespace

namespace {
TEST(TypedRecordTest, PartialArrayCountPrefixReportsWrittenExtent) {
    struct Value {
        uint8_t items[128];
        uint32_t count;
    };
    const quarry_c_brf_v2_field_layout_t slot = {
        0U, 0U, 17U, 0U, 8U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR};
    const quarry_c_brf_v2_record_layout_t wire = {42U, 1U, 9U, 1U, &slot};
    const quarry_c_member_descriptor_t member = {
        QUARRY_C_COUNT_PRESENCE(offsetof(Value, count)),
        offsetof(Value, items),
        offsetof(Value, count),
        128U,
        {QUARRY_C_TYPED_U8, 1U, 0U, 0U, 1U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}};
    const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Value), &member, nullptr};
    Value value{};
    value.count = 128U; // Two-byte varuint count.
    for (const size_t capacity : {26U, 27U, 28U}) {
        std::array<uint8_t, 160> output{};
        output.fill(0xA5U);
        const auto result =
            quarry_c_typed_encode(&descriptor, &value, 42U, output.data(), capacity);
        EXPECT_EQ(result.status, QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);
        EXPECT_EQ(result.bytes_written, capacity);
        EXPECT_EQ(output[25U], 0x80U);
        for (size_t i = result.bytes_written; i < output.size(); ++i)
            EXPECT_EQ(output[i], 0xA5U);
    }
}
} // namespace

namespace {
TEST(TypedRecordTest, ScalarDecodeRejectsShortSlotsWithoutWritingValue) {
    for (size_t i = 0U; i < wire_fields.size(); ++i) {
        for (uint32_t length = 0U; length < members[i].element.wire_width; ++length) {
            auto field = wire_fields[i];
            field.presence_bit_index = 0U;
            field.byte_offset = 17U;
            field.slot_size = length;
            auto binding = members[i];
            binding.element.wire_width = length;
            const quarry_c_brf_v2_record_layout_t wire_layout = {42U, 1U, 1U + length, 1U, &field};
            const quarry_c_type_descriptor_t descriptor = {&wire_layout, sizeof(Scalars), &binding,
                                                           nullptr};
            std::array<uint8_t, 25> wire{};
            wire[0] = QUARRY_C_BRF_V2_FORMAT_VERSION;
            quarry_c_brf_v2_write_u16(wire.data() + 2U, QUARRY_C_BRF_V2_HEADER_SIZE);
            quarry_c_brf_v2_write_u32(wire.data() + 4U, 42U);
            quarry_c_brf_v2_write_u32(wire.data() + 8U, 1U + length);
            quarry_c_brf_v2_write_u32(wire.data() + 12U, 17U + length);
            wire[16] = 1U;
            Scalars decoded;
            std::memset(&decoded, 0xa5, sizeof(decoded));
            const auto result =
                quarry_c_typed_decode(&descriptor, wire.data(), 17U + length, 42U, &decoded);
            EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_FIELD_LENGTH) << "field " << i;
            EXPECT_EQ(result.byte_offset, 17U);
            const auto* native = reinterpret_cast<const uint8_t*>(&decoded) + binding.value_offset;
            for (size_t j = 0U; j < binding.element.size; ++j)
                EXPECT_EQ(native[j], 0xa5U);
        }
    }
}

TEST(TypedRecordTest, ScalarDecodePreservesWireBitsFromUnalignedInput) {
    // Build the wire values independently of scalar_encode, including signed
    // extrema and float bit patterns. Offset the buffer to exercise memcpy stores
    // and byte loads without alignment assumptions.
    for (const uint8_t pattern : {0U, 0x7fU, 0x80U, 0xffU}) {
        std::array<uint8_t, 62> bytes{};
        auto* wire = bytes.data() + 1U;
        wire[0] = QUARRY_C_BRF_V2_FORMAT_VERSION;
        quarry_c_brf_v2_write_u16(wire + 2U, QUARRY_C_BRF_V2_HEADER_SIZE);
        quarry_c_brf_v2_write_u32(wire + 4U, 42U);
        quarry_c_brf_v2_write_u32(wire + 8U, 45U);
        quarry_c_brf_v2_write_u32(wire + 12U, 61U);
        wire[16] = 0xffU;
        wire[17] = 7U;
        wire[18] = 1U;
        for (size_t i = 1U; i < wire_fields.size(); ++i) {
            const auto& field = wire_fields[i];
            std::memset(wire + field.byte_offset, pattern, field.slot_size);
        }
        Scalars decoded{};
        const auto result = quarry_c_typed_decode(&type, wire, 61U, 42U, &decoded);
        ASSERT_EQ(result.status, QUARRY_C_STATUS_OK);
        for (size_t i = 1U; i < wire_fields.size(); ++i) {
            const auto* native =
                reinterpret_cast<const uint8_t*>(&decoded) + members[i].value_offset;
            for (size_t j = 0U; j < members[i].element.size; ++j)
                EXPECT_EQ(native[j], pattern) << "field " << i << " byte " << j;
        }
    }
}

TEST(TypedRecordTest, DirectFloatWritesPreserveNanPayloadAndNegativeZeroBits) {
    for (const bool nan : {true, false}) {
        const uint32_t f32_bits = nan ? UINT32_C(0x7fc12345) : UINT32_C(0x80000000);
        const uint64_t f64_bits = nan ? UINT64_C(0x7ff8000000001234) : UINT64_C(0x8000000000000000);
        Scalars value{};
        value.has_f32 = value.has_f64 = true;
        std::memcpy(&value.f32, &f32_bits, sizeof(f32_bits));
        std::memcpy(&value.f64, &f64_bits, sizeof(f64_bits));
        std::array<uint8_t, 128> bytes{};
        const auto encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), bytes.size());
        ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
        EXPECT_EQ(quarry_c_brf_v2_read_u32(bytes.data() + wire_fields[9].byte_offset), f32_bits);
        const uint8_t* f64_wire = bytes.data() + wire_fields[10].byte_offset;
        EXPECT_EQ(quarry_c_brf_v2_read_u32(f64_wire), static_cast<uint32_t>(f64_bits >> 32U));
        EXPECT_EQ(quarry_c_brf_v2_read_u32(f64_wire + 4U), static_cast<uint32_t>(f64_bits));
        Scalars decoded{};
        ASSERT_EQ(
            quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded).status,
            QUARRY_C_STATUS_OK);
        uint32_t decoded32;
        uint64_t decoded64;
        std::memcpy(&decoded32, &decoded.f32, sizeof(decoded32));
        std::memcpy(&decoded64, &decoded.f64, sizeof(decoded64));
        EXPECT_EQ(decoded32, f32_bits);
        EXPECT_EQ(decoded64, f64_bits);
    }
}

TEST(TypedRecordTest, FindNonzeroMatchesBytewiseScanForEveryLengthAndPosition) {
    // Covers the word loop, the byte tail, and unaligned starts.
    std::array<uint8_t, 24> storage{};
    for (uint32_t start = 0U; start < 4U; ++start) {
        for (uint32_t n = 0U; n + start <= 20U; ++n) {
            const uint8_t* p = storage.data() + start;
            EXPECT_EQ(quarry_c_brf_v2_find_nonzero(p, n), n);
            for (uint32_t hot = 0U; hot < n; ++hot) {
                for (const uint8_t pattern : {uint8_t{1U}, uint8_t{0x80U}}) {
                    storage[start + hot] = pattern;
                    if (hot + 1U < n)
                        storage[start + n - 1U] = 0xFFU; // A later nonzero must not win.
                    EXPECT_EQ(quarry_c_brf_v2_find_nonzero(p, n), hot)
                        << "start " << start << " n " << n;
                    storage.fill(0U);
                }
            }
        }
    }
}

TEST(TypedRecordTest, AbsentSlotNonzeroByteReportsItsExactOffset) {
    Scalars value{};
    std::array<uint8_t, 128> clean{};
    const auto encoded = quarry_c_typed_encode(&type, &value, 42U, clean.data(), clean.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    for (const auto& field : wire_fields) {
        for (uint32_t j = 0U; j < field.slot_size; ++j) {
            auto bytes = clean;
            bytes[field.byte_offset + j] = 0x40U;
            Scalars decoded{};
            const auto result =
                quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
            EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_FIELD_RANGE);
            EXPECT_TRUE(result.has_byte_offset);
            EXPECT_EQ(result.byte_offset, field.byte_offset + j);
        }
    }
}

TEST(TypedRecordTest, StructuralErrorsTakePrecedenceOverEarlierValueErrors) {
    // The bool (first field) holds an invalid value; a later absent slot is
    // nonzero. Validation of the whole structure wins, as in two-pass decoding.
    Scalars value{};
    value.has_boolean = true;
    value.boolean = true;
    std::array<uint8_t, 128> bytes{};
    const auto encoded = quarry_c_typed_encode(&type, &value, 42U, bytes.data(), bytes.size());
    ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    bytes[18] = 2U;
    const size_t late = wire_fields[10].byte_offset + 5U; // Absent f64 slot.
    bytes[late] = 1U;
    Scalars decoded{};
    auto result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_FIELD_RANGE);
    EXPECT_EQ(result.byte_offset, late);
    bytes[late] = 0U; // With the structure valid, the held value error is reported.
    result = quarry_c_typed_decode(&type, bytes.data(), encoded.bytes_written, 42U, &decoded);
    EXPECT_EQ(result.status, QUARRY_C_STATUS_INVALID_BOOL);
    EXPECT_EQ(result.byte_offset, 18U);
}

TEST(TypedRecordTest, SinglePassDecodeMatchesValidateThenDecodeReference) {
    // Reference: validate with the public two-step parser, then apply the only
    // value rule this schema has (bool must be 0 or 1). Mutate one or two bytes
    // of valid sparse and dense records and require identical status/offset,
    // and identical values on success.
    Scalars dense{};
    dense.has_boolean = dense.has_i8 = dense.has_u16 = dense.has_i32 = dense.has_u64 = true;
    dense.has_f32 = dense.has_f64 = true;
    dense.boolean = true;
    dense.i8 = -3;
    dense.u16 = 0xBEEFU;
    dense.i32 = -123456;
    dense.u64 = UINT64_C(0x0123456789ABCDEF);
    dense.f32 = 2.5F;
    dense.f64 = -0.125;
    uint32_t state = 12345U;
    const auto next = [&state] {
        state = state * 1103515245U + 12345U;
        return state >> 8U;
    };
    for (const Scalars& source : {Scalars{}, dense}) {
        std::array<uint8_t, 128> clean{};
        const auto encoded = quarry_c_typed_encode(&type, &source, 42U, clean.data(), clean.size());
        ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
        const size_t length = encoded.bytes_written;
        for (int trial = 0; trial < 4000; ++trial) {
            auto bytes = clean;
            const int mutations = 1 + static_cast<int>(next() % 2U);
            for (int m = 0; m < mutations; ++m)
                bytes[16U + next() % (length - 16U)] = static_cast<uint8_t>(next());
            quarry_c_brf_v2_parsed_record_t parsed{};
            size_t expected_offset = 0U;
            quarry_c_status_t expected = quarry_c_brf_v2_parse_record(bytes.data(), length, &layout,
                                                                      &parsed, &expected_offset);
            if (expected == QUARRY_C_STATUS_OK && quarry_c_brf_v2_present_bit(bytes.data(), 0U) &&
                bytes[wire_fields[0].byte_offset] > 1U) {
                expected = QUARRY_C_STATUS_INVALID_BOOL;
                expected_offset = wire_fields[0].byte_offset;
            }
            Scalars decoded;
            std::memset(&decoded, 0xA5, sizeof(decoded));
            const auto result = quarry_c_typed_decode(&type, bytes.data(), length, 42U, &decoded);
            ASSERT_EQ(result.status, expected) << "trial " << trial;
            ASSERT_EQ(result.has_byte_offset, expected != QUARRY_C_STATUS_OK);
            if (expected != QUARRY_C_STATUS_OK) {
                ASSERT_EQ(result.byte_offset, expected_offset) << "trial " << trial;
                continue;
            }
            // Success: every member equals its wire view, presence included.
            Scalars reencoded_source = decoded;
            std::array<uint8_t, 128> reencoded{};
            const auto again = quarry_c_typed_encode(&type, &reencoded_source, 42U,
                                                     reencoded.data(), reencoded.size());
            ASSERT_EQ(again.status, QUARRY_C_STATUS_OK);
            ASSERT_EQ(again.bytes_written, length);
            ASSERT_EQ(0, std::memcmp(reencoded.data(), bytes.data(), length)) << "trial " << trial;
        }
    }
}

// Wide records: more than 32 presence bits, with presence-bit order permuted
// relative to field order (bit = 7 * field mod count; 7 is coprime to both
// counts). 45 bits use a 6-byte bitmap (not a multiple of 4, with unused
// trailing bits); 130 bits exceed the per-call bitmap cache and use the
// per-byte fallback. Every decode is checked against the reference parser.
template <uint32_t Count> void check_wide_permuted_presence() {
    struct Wide {
        bool has[Count];
        uint8_t v[Count];
    };
    constexpr uint32_t bitmap = (Count + 7U) / 8U;
    std::array<quarry_c_brf_v2_field_layout_t, Count> slots{};
    std::array<quarry_c_member_descriptor_t, Count> bindings{};
    for (uint32_t i = 0U; i < Count; ++i) {
        slots[i] = {
            i, (7U * i) % Count, 16U + bitmap + i, 0U, 8U, 1U, QUARRY_C_BRF_V2_STORAGE_FIXED};
        bindings[i] = {
            offsetof(Wide, has) + i,
            offsetof(Wide, v) + i,
            0U,
            0U,
            {QUARRY_C_TYPED_U8, 1U, 0U, 0U, 1U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}};
    }
    const quarry_c_brf_v2_record_layout_t wire = {42U, bitmap, bitmap + Count, Count, slots.data()};
    const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Wide), bindings.data(), nullptr};
    uint32_t state = 777U + Count;
    const auto next = [&state] {
        state = state * 1103515245U + 12345U;
        return state >> 8U;
    };
    for (int round = 0; round < 40; ++round) {
        Wide value{};
        for (uint32_t i = 0U; i < Count; ++i) {
            value.has[i] = (next() % 3U) != 0U;
            value.v[i] = static_cast<uint8_t>(next());
        }
        std::array<uint8_t, 16U + bitmap + Count> clean{};
        const auto encoded =
            quarry_c_typed_encode(&descriptor, &value, 42U, clean.data(), clean.size());
        ASSERT_EQ(encoded.status, QUARRY_C_STATUS_OK);
        ASSERT_EQ(encoded.bytes_written, clean.size());
        for (uint32_t i = 0U; i < Count; ++i)
            ASSERT_EQ(quarry_c_brf_v2_present_bit(clean.data(), slots[i].presence_bit_index),
                      value.has[i]);
        for (int trial = 0; trial < 200; ++trial) {
            auto bytes = clean;
            if (trial != 0) { // Trial 0 decodes the unmodified record.
                const int mutations = 1 + static_cast<int>(next() % 2U);
                for (int m = 0; m < mutations; ++m) {
                    // Bias toward the bitmap so presence flips are common.
                    const size_t at = (next() % 2U) != 0U ? 16U + next() % bitmap
                                                          : 16U + next() % (clean.size() - 16U);
                    bytes[at] = static_cast<uint8_t>(next());
                }
            }
            quarry_c_brf_v2_parsed_record_t parsed{};
            size_t expected_offset = 0U;
            const quarry_c_status_t expected = quarry_c_brf_v2_parse_record(
                bytes.data(), bytes.size(), &wire, &parsed, &expected_offset);
            Wide decoded;
            std::memset(&decoded, 0xA5, sizeof(decoded));
            const auto result =
                quarry_c_typed_decode(&descriptor, bytes.data(), bytes.size(), 42U, &decoded);
            ASSERT_EQ(result.status, expected) << Count << " round " << round << " trial " << trial;
            if (expected != QUARRY_C_STATUS_OK) {
                ASSERT_EQ(result.byte_offset, expected_offset) << Count << " trial " << trial;
                continue;
            }
            for (uint32_t i = 0U; i < Count; ++i) {
                const bool present =
                    quarry_c_brf_v2_present_bit(bytes.data(), slots[i].presence_bit_index);
                ASSERT_EQ(decoded.has[i], present) << Count << " field " << i;
                ASSERT_EQ(decoded.v[i], present ? bytes[slots[i].byte_offset] : 0U)
                    << Count << " field " << i;
            }
        }
    }
}

TEST(TypedRecordTest, WidePermutedPresenceMatchesReference45Fields) {
    check_wide_permuted_presence<45U>();
}

TEST(TypedRecordTest, WidePermutedPresenceBeyondCacheMatchesReference130Fields) {
    check_wide_permuted_presence<130U>();
}

TEST(TypedRecordTest, EmptyLayoutWithoutBitmapMatchesReference) {
    struct Empty {
        uint8_t unused;
    };
    const quarry_c_brf_v2_record_layout_t wire = {42U, 0U, 0U, 0U, nullptr};
    const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Empty), nullptr, nullptr};
    std::array<uint8_t, 17> bytes{};
    bytes[0] = QUARRY_C_BRF_V2_FORMAT_VERSION;
    quarry_c_brf_v2_write_u16(bytes.data() + 2U, QUARRY_C_BRF_V2_HEADER_SIZE);
    quarry_c_brf_v2_write_u32(bytes.data() + 4U, 42U);
    for (const size_t length : {size_t{16}, size_t{17}}) {
        quarry_c_brf_v2_write_u32(bytes.data() + 12U, static_cast<uint32_t>(length));
        quarry_c_brf_v2_parsed_record_t parsed{};
        size_t expected_offset = 0U;
        const auto expected =
            quarry_c_brf_v2_parse_record(bytes.data(), length, &wire, &parsed, &expected_offset);
        Empty decoded{};
        const auto result = quarry_c_typed_decode(&descriptor, bytes.data(), length, 42U, &decoded);
        EXPECT_EQ(result.status, expected) << length;
        if (expected != QUARRY_C_STATUS_OK)
            EXPECT_EQ(result.byte_offset, expected_offset);
    }
}
} // namespace

namespace {
// Derives decode operations from hand-built descriptors with the generator's
// rules (compiler/backend_c render_decode_ops), so the runtime op path can be
// compared with the descriptor-only path on the same fixtures.
std::vector<quarry_c_field_decode_op_t> derive_ops(const quarry_c_type_descriptor_t& type) {
    std::vector<quarry_c_field_decode_op_t> ops(type.layout->field_count);
    for (uint32_t i = 0U; i < type.layout->field_count; ++i) {
        const auto& f = type.layout->fields[i];
        const auto& m = type.members[i];
        const auto& e = m.element;
        auto& op = ops[i];
        op.wire_offset = f.byte_offset;
        op.slot_size = f.slot_size;
        op.presence_byte = 16U + f.presence_bit_index / 8U;
        op.presence_mask = static_cast<uint8_t>(1U << (f.presence_bit_index % 8U));
        op.value_offset = m.value_offset;
        const bool counted = (m.presence_offset & QUARRY_C_PRESENCE_COUNT_FLAG) != 0U;
        const bool packed = (m.presence_offset & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U;
        if (counted) {
            op.presence = m.max_count == 0U && e.kind == QUARRY_C_TYPED_STRING
                              ? QUARRY_C_DECODE_PRESENCE_COUNT_STRING
                              : QUARRY_C_DECODE_PRESENCE_COUNT;
            op.presence_offset = m.presence_offset & ~QUARRY_C_PRESENCE_COUNT_FLAG;
        } else if (packed) {
            const size_t bit = m.presence_offset & ~QUARRY_C_PRESENCE_PACKED_FLAG;
            op.presence = bit % 8U == 0U ? QUARRY_C_DECODE_PRESENCE_PACKED_FIRST
                                         : QUARRY_C_DECODE_PRESENCE_PACKED;
            op.presence_offset = bit / 8U;
            op.native_mask = static_cast<uint8_t>(1U << (bit % 8U));
        } else {
            op.presence = QUARRY_C_DECODE_PRESENCE_BOOL;
            op.presence_offset = m.presence_offset;
        }
        op.clear_size = (counted || e.kind == QUARRY_C_TYPED_RECORD) ? 0U : e.size;
        op.kind = f.storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR
                      ? QUARRY_C_DECODE_VARIABLE_FIELD
                      : QUARRY_C_DECODE_FIXED_FIELD;
        if (f.storage == QUARRY_C_BRF_V2_STORAGE_FIXED && m.max_count == 0U &&
            e.kind <= QUARRY_C_TYPED_ENUM) {
            const bool exact = f.slot_size == e.wire_width && e.size == e.wire_width;
            if (e.kind == QUARRY_C_TYPED_ENUM || !exact)
                op.kind = QUARRY_C_DECODE_SCALAR;
            else if (e.kind == QUARRY_C_TYPED_BOOL)
                op.kind = QUARRY_C_DECODE_BOOL;
            else if (e.wire_width == 1U)
                op.kind = QUARRY_C_DECODE_FIXED_1;
            else if (e.wire_width == 2U)
                op.kind = QUARRY_C_DECODE_FIXED_2;
            else if (e.wire_width == 4U)
                op.kind = QUARRY_C_DECODE_FIXED_4;
            else if (e.wire_width == 8U)
                op.kind = QUARRY_C_DECODE_FIXED_8;
            else
                op.kind = QUARRY_C_DECODE_SCALAR;
        }
    }
    return ops;
}

// Decodes every seed and thousands of one/two-byte mutations of it through the
// descriptor-only path and the op path, requiring identical status, offset, and
// (on success) identical destination bytes from identical dirty starting state.
void expect_ops_match_descriptors(const quarry_c_type_descriptor_t& type,
                                  const std::vector<std::vector<uint8_t>>& seeds,
                                  uint32_t seed_state) {
    const auto ops = derive_ops(type);
    quarry_c_type_descriptor_t with_ops = type;
    with_ops.decode_ops = ops.data();
    uint32_t state = seed_state;
    const auto next = [&state] {
        state = state * 1103515245U + 12345U;
        return state >> 8U;
    };
    size_t failures = 0U;
    size_t successes = 0U;
    for (const auto& seed : seeds) {
        ASSERT_GT(seed.size(), 16U);
        for (int trial = 0; trial < 3000; ++trial) {
            auto bytes = seed;
            if (trial != 0) {
                const int mutations = 1 + static_cast<int>(next() % 2U);
                for (int m = 0; m < mutations; ++m) {
                    // Half the mutations use boundary values (e.g. bool 2).
                    static constexpr uint8_t boundary[] = {0U, 1U, 2U, 0x80U, 0xFFU};
                    const uint8_t v =
                        (next() % 2U) != 0U ? boundary[next() % 5U] : static_cast<uint8_t>(next());
                    bytes[16U + next() % (bytes.size() - 16U)] = v;
                }
            }
            std::vector<uint8_t> expected_value(type.size, 0xA5U);
            std::vector<uint8_t> actual_value(type.size, 0xA5U);
            const uint32_t id = type.layout->record_id;
            const auto expected =
                quarry_c_typed_decode(&type, bytes.data(), bytes.size(), id, expected_value.data());
            const auto actual = quarry_c_typed_decode(&with_ops, bytes.data(), bytes.size(), id,
                                                      actual_value.data());
            ASSERT_EQ(actual.status, expected.status) << "trial " << trial;
            ASSERT_EQ(actual.has_byte_offset, expected.has_byte_offset);
            ASSERT_EQ(actual.byte_offset, expected.byte_offset) << "trial " << trial;
            if (expected.status == QUARRY_C_STATUS_OK) {
                ++successes;
                ASSERT_EQ(actual_value, expected_value) << "trial " << trial;
            } else {
                ++failures;
            }
        }
    }
    EXPECT_GT(successes, 0U); // Both outcomes must actually be exercised.
    EXPECT_GT(failures, 0U);
}

std::vector<uint8_t> encode_seed(const quarry_c_type_descriptor_t& type, const void* value) {
    std::vector<uint8_t> bytes(512U);
    const auto encoded =
        quarry_c_typed_encode(&type, value, type.layout->record_id, bytes.data(), bytes.size());
    EXPECT_EQ(encoded.status, QUARRY_C_STATUS_OK);
    bytes.resize(encoded.bytes_written);
    return bytes;
}

TEST(TypedRecordTest, DecodeOpsMatchDescriptorPathForScalarsBoolAndPackedPresence) {
    Scalars dense{};
    dense.has_boolean = dense.has_i8 = dense.has_u8 = dense.has_i16 = dense.has_u16 = true;
    dense.has_i32 = dense.has_u32 = dense.has_i64 = dense.has_u64 = true;
    dense.has_f32 = dense.has_f64 = true;
    dense.boolean = true;
    dense.i16 = -2;
    dense.u32 = 0xA1B2C3D4U;
    dense.i64 = INT64_MIN;
    dense.f64 = 6.25;
    Scalars sparse{};
    sparse.has_u8 = sparse.has_f32 = true;
    sparse.u8 = 9U;
    sparse.f32 = -0.5F;
    const Scalars none{};
    expect_ops_match_descriptors(
        type, {encode_seed(type, &dense), encode_seed(type, &sparse), encode_seed(type, &none)},
        1U);

    struct Packed {
        uint8_t bits[2];
        Scalars values;
    };
    std::array<quarry_c_member_descriptor_t, 11> bindings{};
    for (size_t i = 0U; i < bindings.size(); ++i) {
        bindings[i] = members[i];
        bindings[i].presence_offset = QUARRY_C_PACKED_PRESENCE(offsetof(Packed, bits), i);
        bindings[i].value_offset += offsetof(Packed, values);
    }
    const quarry_c_type_descriptor_t packed = {&layout, sizeof(Packed), bindings.data(), nullptr};
    Packed packed_value{};
    packed_value.bits[0] = 0x8DU;
    packed_value.bits[1] = 0x05U;
    packed_value.values.u16 = 0x1234U;
    packed_value.values.u64 = UINT64_MAX;
    const Packed packed_none{};
    expect_ops_match_descriptors(
        packed, {encode_seed(packed, &packed_value), encode_seed(packed, &packed_none)}, 2U);
}

TEST(TypedRecordTest, DecodeOpsMatchDescriptorPathForVariableFields) {
    VariableFields value{};
    std::memcpy(value.text, "abc", 3U);
    value.text_length = 3U;
    value.bytes[0] = 0xFFU;
    value.bytes_length = 1U;
    value.items[0] = 1U;
    value.items[1] = 2U;
    value.items_count = 2U;
    VariableFields only_items{};
    only_items.items_count = 4U;
    expect_ops_match_descriptors(
        variable_type,
        {encode_seed(variable_type, &value), encode_seed(variable_type, &only_items)}, 3U);
}

TEST(TypedRecordTest, DecodeOpsMatchDescriptorPathForWidePermutedPresence) {
    for (const uint32_t count : {45U, 130U}) {
        struct Wide {
            bool has[130];
            uint8_t v[130];
        };
        const uint32_t bitmap = (count + 7U) / 8U;
        std::vector<quarry_c_brf_v2_field_layout_t> slots(count);
        std::vector<quarry_c_member_descriptor_t> bindings(count);
        for (uint32_t i = 0U; i < count; ++i) {
            slots[i] = {
                i, (7U * i) % count, 16U + bitmap + i, 0U, 8U, 1U, QUARRY_C_BRF_V2_STORAGE_FIXED};
            bindings[i] = {offsetof(Wide, has) + i,
                           offsetof(Wide, v) + i,
                           0U,
                           0U,
                           {QUARRY_C_TYPED_U8, 1U, 0U, 0U, 1U, nullptr, 0U, false, nullptr, 0U,
                            nullptr, nullptr}};
        }
        const quarry_c_brf_v2_record_layout_t wire = {42U, bitmap, bitmap + count, count,
                                                      slots.data()};
        const quarry_c_type_descriptor_t descriptor = {&wire, sizeof(Wide), bindings.data(),
                                                       nullptr};
        Wide value{};
        for (uint32_t i = 0U; i < count; i += 3U) {
            value.has[i] = true;
            value.v[i] = static_cast<uint8_t>(i * 5U);
        }
        const Wide wide_none{};
        expect_ops_match_descriptors(
            descriptor, {encode_seed(descriptor, &value), encode_seed(descriptor, &wide_none)},
            count);
    }
}
} // namespace
