#include <quarry/runtime_c/typed_record.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

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
const quarry_c_type_descriptor_t type = {&layout, sizeof(Scalars), members};
#undef SCALARS

TEST(TypedRecordTest, PackedPresenceCrossesByteBoundaryAndPreservesScalarZero) {
    struct Packed { uint8_t bits[2]; Scalars values; };
    std::array<quarry_c_member_descriptor_t, 11> bindings{};
    for (size_t i = 0U; i < bindings.size(); ++i) {
        bindings[i] = members[i];
        bindings[i].presence_offset = QUARRY_C_PACKED_PRESENCE(offsetof(Packed, bits), i);
        bindings[i].value_offset += offsetof(Packed, values);
    }
    const quarry_c_type_descriptor_t packed_type = {&layout, sizeof(Packed), bindings.data()};
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

TEST(TypedRecordTest, CapacityFailurePreservesOutputAndAbsentValuesStayZero) {
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
    {0U, offsetof(VariableFields, text), 0U, 0U,
     {QUARRY_C_TYPED_STRING, 5U, offsetof(VariableFields, text_length) - offsetof(VariableFields, text),
      4U, 0U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}},
    {0U, offsetof(VariableFields, bytes), 0U, 0U,
     {QUARRY_C_TYPED_BYTES, 4U, offsetof(VariableFields, bytes_length) - offsetof(VariableFields, bytes),
      4U, 0U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}},
    {0U, offsetof(VariableFields, items), offsetof(VariableFields, items_count), 4U,
     {QUARRY_C_TYPED_U8, 1U, 0U, 0U, 1U, nullptr, 0U, false, nullptr, 0U, nullptr, nullptr}},
};
const quarry_c_type_descriptor_t variable_type = {&variable_layout, sizeof(VariableFields), variable_members};

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
