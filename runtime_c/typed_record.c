#include <quarry/runtime_c/typed_record.h>

/* All host accesses use memcpy: no alignment or aliasing assumptions. */
static uint32_t load_count(const uint8_t* p) {
    uint32_t n;
    memcpy(&n, p, sizeof(n));
    return n;
}

static bool enum_valid(const quarry_c_element_descriptor_t* e, uint64_t value) {
    for (size_t i = 0; i < e->enum_count; ++i) {
        if (e->enum_values[i] == value)
            return true;
    }
    return false;
}

static quarry_c_status_t scalar_encode(const quarry_c_element_descriptor_t* e, const uint8_t* p,
                                       uint8_t* output) {
    quarry_c_writer_t writer = {output, 8U, 0U};
    switch (e->kind) {
#define ENCODE(K, T, V)                                                                            \
    case QUARRY_C_TYPED_##K: {                                                                     \
        T v;                                                                                       \
        memcpy(&v, p, sizeof(v));                                                                  \
        return quarry_c_write_##V(&writer, v);                                                     \
    }
        ENCODE(BOOL, bool, bool)
        ENCODE(I8, int8_t, i8)
        ENCODE(U8, uint8_t, u8)
        ENCODE(I16, int16_t, i16)
        ENCODE(U16, uint16_t, u16)
        ENCODE(I32, int32_t, i32)
        ENCODE(U32, uint32_t, u32)
        ENCODE(I64, int64_t, i64)
        ENCODE(U64, uint64_t, u64)
        ENCODE(F32, float, f32)
        ENCODE(F64, double, f64)
#undef ENCODE
    case QUARRY_C_TYPED_ENUM: {
        uint64_t v = e->enum_load(p);
        if (!enum_valid(e, v))
            return QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;
        for (size_t i = 0; i < e->wire_width; ++i) {
            output[e->wire_width - i - 1U] = (uint8_t)v;
            v >>= 8U;
        }
        return QUARRY_C_STATUS_OK;
    }
    default:
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
}

static quarry_c_status_t scalar_decode(const quarry_c_element_descriptor_t* e, const uint8_t* input,
                                       size_t length, uint8_t* p) {
    if (length != e->wire_width)
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    quarry_c_reader_t reader = {input, length, 0U};
    switch (e->kind) {
#define DECODE(K, T, V)                                                                            \
    case QUARRY_C_TYPED_##K: {                                                                     \
        T v = 0;                                                                                   \
        quarry_c_status_t s = quarry_c_read_##V(&reader, &v);                                      \
        if (s == QUARRY_C_STATUS_OK)                                                               \
            memcpy(p, &v, sizeof(v));                                                              \
        return s;                                                                                  \
    }
        DECODE(BOOL, bool, bool)
        DECODE(I8, int8_t, i8)
        DECODE(U8, uint8_t, u8)
        DECODE(I16, int16_t, i16)
        DECODE(U16, uint16_t, u16)
        DECODE(I32, int32_t, i32)
        DECODE(U32, uint32_t, u32)
        DECODE(I64, int64_t, i64)
        DECODE(U64, uint64_t, u64)
        DECODE(F32, float, f32)
        DECODE(F64, double, f64)
#undef DECODE
    case QUARRY_C_TYPED_ENUM: {
        uint64_t v = 0;
        for (size_t i = 0; i < length; ++i)
            v = (v << 8U) | input[i];
        if (!enum_valid(e, v))
            return QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;
        e->enum_store(p, v);
        return QUARRY_C_STATUS_OK;
    }
    default:
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
}

static quarry_c_status_t measure_record(const quarry_c_type_descriptor_t*, const uint8_t*, size_t*);

static quarry_c_status_t measure_element(const quarry_c_element_descriptor_t* e, const uint8_t* p,
                                         size_t* size) {
    if (e->kind == QUARRY_C_TYPED_RECORD)
        return measure_record(e->record, p, size);
    if (e->kind == QUARRY_C_TYPED_STRING || e->kind == QUARRY_C_TYPED_BYTES) {
        *size = load_count(p + e->length_offset);
        if (*size > e->bound)
            return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
        if (e->kind == QUARRY_C_TYPED_STRING && !quarry_c_is_valid_utf8(p, *size))
            return QUARRY_C_STATUS_INVALID_UTF8;
        return QUARRY_C_STATUS_OK;
    }
    uint8_t scratch[8];
    *size = e->wire_width;
    return scalar_encode(e, p, scratch);
}

static quarry_c_status_t add_size(size_t* total, size_t n) {
    if (n > UINT32_MAX || *total > UINT32_MAX - n)
        return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    *total += n;
    return QUARRY_C_STATUS_OK;
}

static size_t prefix_size(size_t n) {
    size_t size = 1U;
    while (n >= 128U) {
        ++size;
        n >>= 7U;
    }
    return size;
}

static bool prefixed_element(const quarry_c_element_descriptor_t* e) {
    return e->kind == QUARRY_C_TYPED_STRING || e->kind == QUARRY_C_TYPED_BYTES ||
           (e->kind == QUARRY_C_TYPED_RECORD && !e->fixed_record);
}

static quarry_c_status_t measure_field(const quarry_c_member_descriptor_t* m, const uint8_t* value,
                                       size_t* size) {
    const uint8_t* p = value + m->value_offset;
    if (m->max_count == 0U)
        return measure_element(&m->element, p, size);
    uint32_t count = load_count(value + m->count_offset);
    if (count > m->max_count)
        return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
    *size = prefix_size(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t n = 0;
        quarry_c_status_t s = measure_element(&m->element, p + i * m->element.size, &n);
        if (s != QUARRY_C_STATUS_OK)
            return s;
        if (prefixed_element(&m->element)) {
            s = add_size(size, prefix_size(n));
            if (s != QUARRY_C_STATUS_OK)
                return s;
        }
        s = add_size(size, n);
        if (s != QUARRY_C_STATUS_OK)
            return s;
    }
    return QUARRY_C_STATUS_OK;
}

static bool member_present(const uint8_t* value, const quarry_c_member_descriptor_t* m) {
    if (m->max_count != 0U)
        return load_count(value + m->count_offset) != 0U;
    if (m->element.kind == QUARRY_C_TYPED_STRING || m->element.kind == QUARRY_C_TYPED_BYTES)
        return load_count(value + m->value_offset + m->element.length_offset) != 0U;
    if ((m->presence_offset & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U) {
        const size_t bit = m->presence_offset & ~QUARRY_C_PRESENCE_PACKED_FLAG;
        return (value[bit / 8U] & (1U << (bit % 8U))) != 0U;
    }
    bool present;
    memcpy(&present, value + m->presence_offset, sizeof(present));
    return present;
}

static quarry_c_status_t measure_record(const quarry_c_type_descriptor_t* type,
                                        const uint8_t* value, size_t* size) {
    *size = 16U;
    quarry_c_status_t s = add_size(size, type->layout->fixed_region_size);
    if (s != QUARRY_C_STATUS_OK)
        return s;
    for (uint32_t i = 0; i < type->layout->field_count; ++i) {
        if (!member_present(value, &type->members[i]))
            continue;
        size_t n = 0;
        s = measure_field(&type->members[i], value, &n);
        if (s != QUARRY_C_STATUS_OK)
            return s;
        if (type->layout->fields[i].storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            s = add_size(size, n);
            if (s != QUARRY_C_STATUS_OK)
                return s;
        } else if (n != type->layout->fields[i].slot_size) {
            return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        }
    }
    return QUARRY_C_STATUS_OK;
}

static void write_record(const quarry_c_type_descriptor_t*, const uint8_t*, uint32_t, uint8_t*,
                         size_t);

static void write_element(const quarry_c_element_descriptor_t* e, const uint8_t* p, uint8_t* output,
                          size_t n) {
    if (e->kind == QUARRY_C_TYPED_RECORD)
        write_record(e->record, p, e->record_id, output, n);
    else if (e->kind == QUARRY_C_TYPED_STRING || e->kind == QUARRY_C_TYPED_BYTES)
        memcpy(output, p, n);
    else
        (void)scalar_encode(e, p, output);
}

static void write_field(const quarry_c_member_descriptor_t* m, const uint8_t* value,
                        uint8_t* output, size_t size) {
    const uint8_t* p = value + m->value_offset;
    if (m->max_count == 0U) {
        write_element(&m->element, p, output, size);
        return;
    }
    uint32_t count = load_count(value + m->count_offset);
    quarry_c_writer_t writer = {output, size, 0U};
    (void)quarry_c_write_varuint(&writer, count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t n = 0;
        (void)measure_element(&m->element, p + i * m->element.size, &n);
        if (prefixed_element(&m->element))
            (void)quarry_c_write_varuint(&writer, n);
        write_element(&m->element, p + i * m->element.size, output + writer.length, n);
        writer.length += n;
    }
}

static void write_record(const quarry_c_type_descriptor_t* type, const uint8_t* value,
                         uint32_t record_id, uint8_t* output, size_t size) {
    memset(output, 0, size);
    output[0] = 2U;
    quarry_c_brf_v2_write_u16(output + 2U, 16U);
    quarry_c_brf_v2_write_u32(output + 4U, record_id);
    quarry_c_brf_v2_write_u32(output + 8U, type->layout->fixed_region_size);
    quarry_c_brf_v2_write_u32(output + 12U, (uint32_t)size);
    size_t tail = 16U + (size_t)type->layout->fixed_region_size;
    for (uint32_t i = 0; i < type->layout->field_count; ++i) {
        const quarry_c_member_descriptor_t* m = &type->members[i];
        const quarry_c_brf_v2_field_layout_t* f = &type->layout->fields[i];
        if (!member_present(value, m))
            continue;
        size_t n = 0;
        (void)measure_field(m, value, &n);
        quarry_c_brf_v2_set_present_bit(output, f->presence_bit_index);
        uint8_t* dest = output + f->byte_offset;
        if (f->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            quarry_c_brf_v2_write_u32(dest, (uint32_t)tail);
            quarry_c_brf_v2_write_u32(dest + 4U, (uint32_t)n);
            dest = output + tail;
            tail += n;
        }
        write_field(m, value, dest, n);
    }
}

quarry_c_typed_encoded_size_result_t quarry_c_typed_encoded_size(
    const quarry_c_type_descriptor_t* type, const void* value) {
    quarry_c_typed_encoded_size_result_t result = {QUARRY_C_STATUS_INVALID_ARGUMENT, 0U};
    if (type == NULL || value == NULL)
        return result;
    size_t size = 0U;
    result.status = measure_record(type, value, &size);
    if (result.status == QUARRY_C_STATUS_OK)
        result.size = size;
    return result;
}

quarry_c_typed_encode_result_t quarry_c_typed_encode(const quarry_c_type_descriptor_t* type,
                                                     const void* value, uint32_t record_id,
                                                     uint8_t* output, size_t capacity) {
    quarry_c_typed_encode_result_t result = {QUARRY_C_STATUS_INVALID_ARGUMENT, 0U};
    if (type == NULL || value == NULL || output == NULL)
        return result;
    size_t size = 0;
    result.status = measure_record(type, value, &size);
    if (result.status != QUARRY_C_STATUS_OK)
        return result;
    if (capacity < size) {
        result.status = QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
        return result;
    }
    write_record(type, value, record_id, output, size);
    result.bytes_written = size;
    return result;
}

static quarry_c_status_t read_record(const quarry_c_type_descriptor_t*, const uint8_t*, size_t,
                                     uint32_t, uint8_t*, size_t*);

static quarry_c_status_t read_element(const quarry_c_element_descriptor_t* e, const uint8_t* input,
                                      size_t length, uint8_t* p, size_t* error) {
    if (e->kind == QUARRY_C_TYPED_RECORD)
        return read_record(e->record, input, length, e->record_id, p, error);
    if (e->kind == QUARRY_C_TYPED_STRING || e->kind == QUARRY_C_TYPED_BYTES) {
        if (length > e->bound)
            return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
        if (e->kind == QUARRY_C_TYPED_STRING && !quarry_c_is_valid_utf8(input, length))
            return QUARRY_C_STATUS_INVALID_UTF8;
        memcpy(p, input, length);
        if (e->kind == QUARRY_C_TYPED_STRING)
            p[length] = 0;
        uint32_t n = (uint32_t)length;
        memcpy(p + e->length_offset, &n, sizeof(n));
        return QUARRY_C_STATUS_OK;
    }
    return scalar_decode(e, input, length, p);
}

static quarry_c_status_t read_field(const quarry_c_member_descriptor_t* m, const uint8_t* input,
                                    size_t length, uint8_t* value, size_t* error) {
    uint8_t* p = value + m->value_offset;
    if (m->max_count == 0U)
        return read_element(&m->element, input, length, p, error);
    quarry_c_reader_t reader = {input, length, 0U};
    uint64_t count = 0;
    quarry_c_status_t s = quarry_c_read_varuint(&reader, &count);
    if (s != QUARRY_C_STATUS_OK)
        return s;
    if (count > m->max_count)
        return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
    for (uint32_t i = 0; i < count; ++i) {
        size_t n = m->element.wire_width;
        if (m->element.kind == QUARRY_C_TYPED_RECORD)
            n = 16U + (size_t)m->element.record->layout->fixed_region_size;
        if (prefixed_element(&m->element)) {
            uint64_t encoded_length = 0;
            s = quarry_c_read_varuint(&reader, &encoded_length);
            if (s != QUARRY_C_STATUS_OK) {
                *error = reader.offset;
                return s;
            }
            if (encoded_length > SIZE_MAX)
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            n = (size_t)encoded_length;
        }
        if (n > reader.length - reader.offset) {
            *error = reader.offset;
            return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        }
        size_t nested_error = 0;
        s = read_element(&m->element, input + reader.offset, n, p + i * m->element.size,
                         &nested_error);
        if (s != QUARRY_C_STATUS_OK) {
            *error = reader.offset + nested_error;
            return s;
        }
        reader.offset += n;
    }
    if (reader.offset != length) {
        *error = reader.offset;
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    uint32_t n = (uint32_t)count;
    memcpy(value + m->count_offset, &n, sizeof(n));
    return QUARRY_C_STATUS_OK;
}

static quarry_c_status_t read_record(const quarry_c_type_descriptor_t* type, const uint8_t* input,
                                     size_t length, uint32_t record_id, uint8_t* value,
                                     size_t* error) {
    quarry_c_brf_v2_record_layout_t layout = *type->layout;
    layout.record_id = record_id;
    quarry_c_brf_v2_parsed_record_t parsed;
    quarry_c_status_t s = quarry_c_brf_v2_parse_record(input, length, &layout, &parsed, error);
    if (s != QUARRY_C_STATUS_OK)
        return s;
    for (uint32_t i = 0; i < layout.field_count; ++i) {
        quarry_c_brf_v2_field_view_t view;
        s = quarry_c_brf_v2_find_field(&parsed, &layout, layout.fields[i].field_index, &view);
        if (s != QUARRY_C_STATUS_OK)
            return s;
        if (!view.present)
            continue;
        size_t nested_error = 0;
        s = read_field(&type->members[i], view.bytes, view.length, value, &nested_error);
        if (s != QUARRY_C_STATUS_OK) {
            *error = view.byte_offset + nested_error;
            return s;
        }
        const quarry_c_member_descriptor_t* member = &type->members[i];
        if (member->max_count == 0U && member->element.kind != QUARRY_C_TYPED_STRING &&
            member->element.kind != QUARRY_C_TYPED_BYTES) {
            if ((member->presence_offset & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U) {
                const size_t bit = member->presence_offset & ~QUARRY_C_PRESENCE_PACKED_FLAG;
                value[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
            } else {
                const bool present = true;
                memcpy(value + member->presence_offset, &present, sizeof(present));
            }
        }
    }
    return QUARRY_C_STATUS_OK;
}

quarry_c_typed_decode_result_t quarry_c_typed_decode(const quarry_c_type_descriptor_t* type,
                                                     const uint8_t* input, size_t length,
                                                     uint32_t record_id, void* value) {
    quarry_c_typed_decode_result_t result = {QUARRY_C_STATUS_INVALID_ARGUMENT, false, 0U};
    if (type == NULL || value == NULL)
        return result;
    memset(value, 0, type->size);
    result.status = read_record(type, input, length, record_id, value, &result.byte_offset);
    result.has_byte_offset = result.status != QUARRY_C_STATUS_OK;
    return result;
}
