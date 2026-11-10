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

/* Fixed slots already have capacity checked. Integers and IEEE floats of the
 * same width share the same bit-preserving big-endian write operation. */
static quarry_c_status_t write_fixed_scalar(const quarry_c_element_descriptor_t* e,
                                            const uint8_t* p, uint8_t* output) {
    if (e->kind == QUARRY_C_TYPED_ENUM) {
        uint64_t v = e->enum_load(p);
        if (!enum_valid(e, v))
            return QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;
        for (size_t i = 0U; i < e->wire_width; ++i) {
            output[e->wire_width - i - 1U] = (uint8_t)v;
            v >>= 8U;
        }
        return QUARRY_C_STATUS_OK;
    }
    if (e->kind == QUARRY_C_TYPED_BOOL) {
        bool v;
        memcpy(&v, p, sizeof(v));
        output[0] = v ? 1U : 0U;
        return QUARRY_C_STATUS_OK;
    }
    switch (e->wire_width) {
    case 1U:
        memcpy(output, p, 1U);
        break;
    case 2U: {
        uint16_t v;
        memcpy(&v, p, sizeof(v));
        quarry_c_brf_v2_write_u16(output, v);
        break;
    }
    case 4U: {
        uint32_t v;
        memcpy(&v, p, sizeof(v));
        quarry_c_brf_v2_write_u32(output, v);
        break;
    }
    case 8U: {
        uint64_t v;
        memcpy(&v, p, sizeof(v));
        quarry_c_brf_v2_write_u32(output, (uint32_t)(v >> 32U));
        quarry_c_brf_v2_write_u32(output + 4U, (uint32_t)v);
        break;
    }
    default:
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
    return QUARRY_C_STATUS_OK;
}

static quarry_c_status_t scalar_decode(const quarry_c_element_descriptor_t* e, const uint8_t* input,
                                       size_t length, uint8_t* p) {
    if (length != e->wire_width)
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    /* The exact field length is checked above. Share bit-preserving loads
     * across integers and IEEE floats, without reader state or alignment
     * assumptions. Bool and enum validation remain explicit. */
    switch (e->kind) {
    case QUARRY_C_TYPED_BOOL: {
        if (length < 1U) return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        if (input[0] > 1U)
            return QUARRY_C_STATUS_INVALID_BOOL;
        const bool v = input[0] != 0U;
        memcpy(p, &v, sizeof(v));
        return QUARRY_C_STATUS_OK;
    }
    case QUARRY_C_TYPED_I8:
    case QUARRY_C_TYPED_U8:
        if (length < 1U) return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        memcpy(p, input, 1U);
        return QUARRY_C_STATUS_OK;
    case QUARRY_C_TYPED_I16:
    case QUARRY_C_TYPED_U16: {
        if (length < 2U) return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        const uint16_t v = quarry_c_brf_v2_read_u16(input);
        memcpy(p, &v, sizeof(v));
        return QUARRY_C_STATUS_OK;
    }
    case QUARRY_C_TYPED_I32:
    case QUARRY_C_TYPED_U32:
    case QUARRY_C_TYPED_F32: {
        if (length < 4U) return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        const uint32_t v = quarry_c_brf_v2_read_u32(input);
        memcpy(p, &v, sizeof(v));
        return QUARRY_C_STATUS_OK;
    }
    case QUARRY_C_TYPED_I64:
    case QUARRY_C_TYPED_U64:
    case QUARRY_C_TYPED_F64: {
        if (length < 8U) return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        const uint64_t v = ((uint64_t)quarry_c_brf_v2_read_u32(input) << 32U) |
                           quarry_c_brf_v2_read_u32(input + 4U);
        memcpy(p, &v, sizeof(v));
        return QUARRY_C_STATUS_OK;
    }
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

static quarry_c_status_t measure_element_impl(const quarry_c_element_descriptor_t* e,
                                              const uint8_t* p, size_t* size) {
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
    *size = e->wire_width;
    switch (e->kind) {
    case QUARRY_C_TYPED_BOOL:
    case QUARRY_C_TYPED_I8:
    case QUARRY_C_TYPED_U8:
    case QUARRY_C_TYPED_I16:
    case QUARRY_C_TYPED_U16:
    case QUARRY_C_TYPED_I32:
    case QUARRY_C_TYPED_U32:
    case QUARRY_C_TYPED_I64:
    case QUARRY_C_TYPED_U64:
    case QUARRY_C_TYPED_F32:
    case QUARRY_C_TYPED_F64:
        /* Every value of these native types is encodable, including IEEE
         * infinities/NaNs. Only enum values need semantic validation here. */
        return QUARRY_C_STATUS_OK;
    case QUARRY_C_TYPED_ENUM:
        return enum_valid(e, e->enum_load(p)) ? QUARRY_C_STATUS_OK
                                              : QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;
    default:
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
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
        return measure_element_impl(&m->element, p, size);
    uint32_t count = load_count(value + m->count_offset);
    if (count > m->max_count)
        return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
    *size = prefix_size(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t n = 0;
        quarry_c_status_t s = measure_element_impl(&m->element, p + i * m->element.size, &n);
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
    if ((m->presence_offset & QUARRY_C_PRESENCE_COUNT_FLAG) != 0U)
        return load_count(value + (m->presence_offset & ~QUARRY_C_PRESENCE_COUNT_FLAG)) != 0U;
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
        if (!member_present(value, &type->members[i])) {
            continue;
        }
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

/* Validation and writing share one traversal. Failure leaves output unspecified. */
static quarry_c_status_t write_record(const quarry_c_type_descriptor_t*, const uint8_t*, uint32_t,
                                      uint8_t*, size_t, size_t*);

static quarry_c_status_t write_element(const quarry_c_element_descriptor_t* e, const uint8_t* p,
                                       uint8_t* output, size_t capacity, size_t* written) {
    if (e->kind == QUARRY_C_TYPED_RECORD)
        return write_record(e->record, p, e->record_id, output, capacity, written);
    if (e->kind == QUARRY_C_TYPED_STRING || e->kind == QUARRY_C_TYPED_BYTES) {
        const size_t n = load_count(p + e->length_offset);
        if (n > e->bound)
            return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
        if (e->kind == QUARRY_C_TYPED_STRING && !quarry_c_is_valid_utf8(p, n))
            return QUARRY_C_STATUS_INVALID_UTF8;
        if (n > capacity)
            return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
        memcpy(output, p, n);
        *written = n;
        return QUARRY_C_STATUS_OK;
    }
    if (capacity < e->wire_width)
        return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
    const quarry_c_status_t status = scalar_encode(e, p, output);
    if (status == QUARRY_C_STATUS_OK)
        *written = e->wire_width;
    return status;
}

static quarry_c_status_t write_field(const quarry_c_member_descriptor_t* m, const uint8_t* value,
                                     uint8_t* output, size_t capacity, size_t* written) {
    const uint8_t* p = value + m->value_offset;
    if (m->max_count == 0U)
        return write_element(&m->element, p, output, capacity, written);
    const uint32_t count = load_count(value + m->count_offset);
    if (count > m->max_count)
        return QUARRY_C_STATUS_BOUNDS_EXCEEDED;
    quarry_c_writer_t writer = {output, capacity, 0U};
    quarry_c_status_t status = quarry_c_write_varuint(&writer, count);
    if (status != QUARRY_C_STATUS_OK) {
        *written = writer.length;
        return status;
    }
    const bool prefixed = prefixed_element(&m->element);
    for (uint32_t i = 0U; i < count; ++i) {
        /* Reserve the minimum prefix, encode once, then expand the prefix if
         * necessary. This accepts exact-size buffers without a sizing pass. */
        const size_t prefix_offset = writer.length;
        if (prefixed) {
            if (writer.length == capacity) {
                *written = writer.length;
                return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
            }
            output[writer.length++] = 0U;
        }
        size_t n = 0U;
        status = write_element(&m->element, p + i * m->element.size, output + writer.length,
                               capacity - writer.length, &n);
        if (status != QUARRY_C_STATUS_OK) {
            *written = writer.length + n;
            return status;
        }
        writer.length += n;
        if (prefixed) {
            const size_t extra = prefix_size(n) - 1U;
            if (extra > capacity - writer.length) {
                *written = writer.length;
                return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
            }
            if (extra != 0U)
                memmove(output + prefix_offset + 1U + extra, output + prefix_offset + 1U, n);
            quarry_c_writer_t prefix = {output + prefix_offset, extra + 1U, 0U};
            (void)quarry_c_write_varuint(&prefix, n);
            writer.length += extra;
        }
    }
    *written = writer.length;
    return QUARRY_C_STATUS_OK;
}

static quarry_c_status_t write_record(const quarry_c_type_descriptor_t* type, const uint8_t* value,
                                      uint32_t record_id, uint8_t* output, size_t capacity,
                                      size_t* written) {
    size_t tail = 16U;
    quarry_c_status_t status = add_size(&tail, type->layout->fixed_region_size);
    if (status != QUARRY_C_STATUS_OK)
        return status;
    if (capacity < tail)
        return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
    memset(output, 0, tail);
    output[0] = 2U;
    quarry_c_brf_v2_write_u16(output + 2U, 16U);
    quarry_c_brf_v2_write_u32(output + 4U, record_id);
    quarry_c_brf_v2_write_u32(output + 8U, type->layout->fixed_region_size);
    for (uint32_t i = 0U; i < type->layout->field_count; ++i) {
        const quarry_c_member_descriptor_t* m = &type->members[i];
        const quarry_c_brf_v2_field_layout_t* f = &type->layout->fields[i];
        if (!member_present(value, m))
            continue;
        const bool variable = f->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR;
        size_t n = 0U;
        if (!variable && m->element.kind <= QUARRY_C_TYPED_ENUM) {
            /* Fixed storage was capacity-checked with the record header. Bypass
             * array/element classification, retaining scalar/enum validation. */
            if (m->element.wire_width > f->slot_size) {
                *written = tail;
                return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
            }
            status =
                write_fixed_scalar(&m->element, value + m->value_offset, output + f->byte_offset);
            n = m->element.wire_width;
        } else {
            status = write_field(m, value, output + (variable ? tail : f->byte_offset),
                                 variable ? capacity - tail : f->slot_size, &n);
        }
        if (status != QUARRY_C_STATUS_OK) {
            *written = variable ? tail + n : tail;
            return status;
        }
        if (variable) {
            quarry_c_brf_v2_write_u32(output + f->byte_offset, (uint32_t)tail);
            quarry_c_brf_v2_write_u32(output + f->byte_offset + 4U, (uint32_t)n);
            tail += n;
        } else if (n != f->slot_size) {
            *written = tail;
            return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
        }
        quarry_c_brf_v2_set_present_bit(output, f->presence_bit_index);
    }
    quarry_c_brf_v2_write_u32(output + 12U, (uint32_t)tail);
    *written = tail;
    return QUARRY_C_STATUS_OK;
}

quarry_c_typed_encoded_size_result_t
quarry_c_typed_encoded_size(const quarry_c_type_descriptor_t* type, const void* value) {
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
    size_t written = 0U;
    result.status = write_record(type, value, record_id, output,
                                 capacity > UINT32_MAX ? UINT32_MAX : capacity, &written);
    result.bytes_written = written;
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

/* True when the n-byte slot is all zero. Generated slots are usually 1, 2, 4
 * or 8 bytes: test those with one or two alignment-free loads. */
static inline bool slot_is_zero(const uint8_t* p, uint32_t n) {
    switch (n) {
    case 1U:
        return p[0] == 0U;
    case 2U: {
        uint16_t v;
        memcpy(&v, p, sizeof(v));
        return v == 0U;
    }
    case 4U: {
        uint32_t v;
        memcpy(&v, p, sizeof(v));
        return v == 0U;
    }
    case 8U: {
        uint32_t v[2];
        memcpy(v, p, sizeof(v));
        return (v[0] | v[1]) == 0U;
    }
    default:
        return quarry_c_brf_v2_find_nonzero(p, n) == n;
    }
}

static inline void clear_native(uint8_t* p, size_t n) {
    switch (n) {
    case 0U:
        return;
    case 1U:
        p[0] = 0U;
        return;
    case 2U: {
        const uint16_t zero = 0U;
        memcpy(p, &zero, sizeof(zero));
        return;
    }
    case 4U: {
        const uint32_t zero = 0U;
        memcpy(p, &zero, sizeof(zero));
        return;
    }
    case 8U: {
        const uint64_t zero = 0U;
        memcpy(p, &zero, sizeof(zero));
        return;
    }
    default:
        memset(p, 0, n);
        return;
    }
}

/* Field traversal using generated decode operations. Same contract, statuses,
 * offsets and precedence as the descriptor-driven loop in read_record; the
 * record header and bitmap were validated by the caller. */
static quarry_c_status_t read_fields_with_ops(const quarry_c_type_descriptor_t* type,
                                              const uint8_t* input, size_t fixed_end,
                                              uint32_t record_size, uint8_t* value,
                                              size_t* error) {
    const quarry_c_field_decode_op_t* op = type->decode_ops;
    const uint32_t count = type->layout->field_count;
    quarry_c_status_t value_status = QUARRY_C_STATUS_OK;
    size_t value_error = 0U;
    size_t tail = fixed_end;
    for (uint32_t i = 0U; i < count; ++i, ++op) {
        if (op->wire_offset > fixed_end || op->slot_size > fixed_end - op->wire_offset) {
            *error = op->wire_offset;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
        const uint8_t* slot = input + op->wire_offset;
        if ((input[op->presence_byte] & op->presence_mask) == 0U) {
            if (!slot_is_zero(slot, op->slot_size)) {
                *error = op->wire_offset + quarry_c_brf_v2_find_nonzero(slot, op->slot_size);
                return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
            }
            if (value_status != QUARRY_C_STATUS_OK)
                continue;
            switch (op->presence) {
            case QUARRY_C_DECODE_PRESENCE_BOOL: {
                const bool absent = false;
                memcpy(value + op->presence_offset, &absent, sizeof(absent));
                break;
            }
            case QUARRY_C_DECODE_PRESENCE_PACKED_FIRST:
                value[op->presence_offset] = 0U;
                break;
            case QUARRY_C_DECODE_PRESENCE_PACKED:
                break;
            case QUARRY_C_DECODE_PRESENCE_COUNT_STRING:
                value[op->value_offset] = 0U;
                /* fall through */
            default: {
                const uint32_t zero = 0U;
                memcpy(value + op->presence_offset, &zero, sizeof(zero));
                break;
            }
            }
            clear_native(value + op->value_offset, op->clear_size);
            continue;
        }
        const uint8_t* bytes = slot;
        size_t n = op->slot_size;
        if (op->kind == QUARRY_C_DECODE_VARIABLE_FIELD) {
            const uint32_t offset = quarry_c_brf_v2_read_u32(slot);
            const uint32_t size = quarry_c_brf_v2_read_u32(slot + 4U);
            if (offset != tail || offset > record_size || size > record_size - offset) {
                *error = op->wire_offset;
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            }
            tail += size;
            bytes = input + offset;
            n = size;
        }
        if (value_status != QUARRY_C_STATUS_OK)
            continue; /* Keep validating structure; the held value error stands. */

        uint8_t* native = value + op->value_offset;
        quarry_c_status_t s = QUARRY_C_STATUS_OK;
        size_t nested_error = 0U;
        switch (op->kind) {
        case QUARRY_C_DECODE_FIXED_1:
            native[0] = bytes[0];
            break;
        case QUARRY_C_DECODE_FIXED_2: {
            const uint16_t v = quarry_c_brf_v2_read_u16(bytes);
            memcpy(native, &v, sizeof(v));
            break;
        }
        case QUARRY_C_DECODE_FIXED_4: {
            const uint32_t v = quarry_c_brf_v2_read_u32(bytes);
            memcpy(native, &v, sizeof(v));
            break;
        }
        case QUARRY_C_DECODE_FIXED_8: {
            const uint64_t v = ((uint64_t)quarry_c_brf_v2_read_u32(bytes) << 32U) |
                               quarry_c_brf_v2_read_u32(bytes + 4U);
            memcpy(native, &v, sizeof(v));
            break;
        }
        case QUARRY_C_DECODE_BOOL:
            if (bytes[0] > 1U) {
                s = QUARRY_C_STATUS_INVALID_BOOL;
            } else {
                const bool v = bytes[0] != 0U;
                memcpy(native, &v, sizeof(v));
            }
            break;
        case QUARRY_C_DECODE_SCALAR:
            s = scalar_decode(&type->members[i].element, bytes, n, native);
            break;
        default: {
            const quarry_c_member_descriptor_t* member = &type->members[i];
            if (op->presence == QUARRY_C_DECODE_PRESENCE_COUNT ||
                op->presence == QUARRY_C_DECODE_PRESENCE_COUNT_STRING) {
                const uint32_t zero = 0U;
                memcpy(value + op->presence_offset, &zero, sizeof(zero));
                if (op->presence == QUARRY_C_DECODE_PRESENCE_COUNT_STRING)
                    value[op->value_offset] = 0U;
            }
            s = read_field(member, bytes, n, value, &nested_error);
            break;
        }
        }
        const bool ok = s == QUARRY_C_STATUS_OK;
        switch (op->presence) {
        case QUARRY_C_DECODE_PRESENCE_BOOL:
            memcpy(value + op->presence_offset, &ok, sizeof(ok));
            break;
        case QUARRY_C_DECODE_PRESENCE_PACKED_FIRST:
            value[op->presence_offset] = ok ? op->native_mask : 0U;
            break;
        case QUARRY_C_DECODE_PRESENCE_PACKED:
            if (ok)
                value[op->presence_offset] |= op->native_mask;
            break;
        default:
            break; /* Count/length presence was stored by read_field. */
        }
        if (!ok) {
            value_status = s;
            value_error = op->wire_offset + nested_error;
        }
    }
    if (tail != record_size) {
        *error = tail;
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    if (value_status != QUARRY_C_STATUS_OK)
        *error = value_error;
    return value_status;
}

/* Validation and decoding share one traversal, reading each presence bit and
 * slot once. Structural errors (header, bitmap, slot bounds, nonzero absent
 * slots, descriptor chaining, final length) take precedence over value errors
 * (bool, enum, UTF-8, bounds, nested records), exactly as when the record was
 * validated in full before decoding: the first value error is held, value
 * decoding stops, and it is returned only once the structure is proven valid.
 * Every byte read was bounds-checked first. Failure leaves value unspecified. */
static quarry_c_status_t read_record(const quarry_c_type_descriptor_t* type, const uint8_t* input,
                                     size_t length, uint32_t record_id, uint8_t* value,
                                     size_t* error) {
    const quarry_c_brf_v2_record_layout_t* layout = type->layout;
    if (input == NULL || length < 16U || input[0] != QUARRY_C_BRF_V2_FORMAT_VERSION ||
        input[1] != 0U || quarry_c_brf_v2_read_u16(input + 2U) != QUARRY_C_BRF_V2_HEADER_SIZE) {
        *error = 0U;
        return QUARRY_C_STATUS_INVALID_HEADER;
    }
    const uint32_t fixed_size = quarry_c_brf_v2_read_u32(input + 8U);
    const uint32_t record_size = quarry_c_brf_v2_read_u32(input + 12U);
    const size_t fixed_end = (size_t)16U + fixed_size;
    if (fixed_size != layout->fixed_region_size || record_size != length || fixed_end > length ||
        record_size < fixed_end || quarry_c_brf_v2_read_u32(input + 4U) != record_id) {
        *error = 8U;
        return fixed_size != layout->fixed_region_size ? QUARRY_C_STATUS_INVALID_FIELD_RANGE
                                                        : QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    if (layout->presence_bitmap_size != 0U && layout->field_count != 0U) {
        const uint32_t used = layout->field_count % 8U;
        const size_t last = 16U + (size_t)layout->presence_bitmap_size - 1U;
        if (used != 0U && (input[last] & (uint8_t)~((1U << used) - 1U)) != 0U) {
            *error = last;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
    }
    if (type->decode_ops != NULL)
        return read_fields_with_ops(type, input, fixed_end, record_size, value, error);
    quarry_c_status_t value_status = QUARRY_C_STATUS_OK;
    size_t value_error = 0U;
    size_t tail = fixed_end;
    size_t cleared_presence_byte = SIZE_MAX;
    for (uint32_t i = 0U; i < layout->field_count; ++i) {
        const quarry_c_brf_v2_field_layout_t* field = &layout->fields[i];
        if (field->byte_offset > fixed_end || field->slot_size > fixed_end - field->byte_offset) {
            *error = field->byte_offset;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
        const uint8_t* slot = input + field->byte_offset;
        const bool present = quarry_c_brf_v2_present_bit(input, field->presence_bit_index);
        const uint8_t* bytes = slot;
        size_t n = field->slot_size;
        if (!present) {
            const uint32_t nonzero = quarry_c_brf_v2_find_nonzero(slot, field->slot_size);
            if (nonzero != field->slot_size) {
                *error = field->byte_offset + nonzero;
                return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
            }
        } else if (field->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            const uint32_t offset = quarry_c_brf_v2_read_u32(slot);
            const uint32_t size = quarry_c_brf_v2_read_u32(slot + 4U);
            if (offset != tail || offset > record_size || size > record_size - offset) {
                *error = field->byte_offset;
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            }
            tail += size;
            bytes = input + offset;
            n = size;
        }
        if (value_status != QUARRY_C_STATUS_OK)
            continue; /* Keep validating structure; the held value error stands. */

        const quarry_c_member_descriptor_t* member = &type->members[i];
        const size_t presence = member->presence_offset;
        const bool counted = (presence & QUARRY_C_PRESENCE_COUNT_FLAG) != 0U;
        const bool packed = (presence & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U;
        if (counted) {
            const uint32_t zero = 0U;
            memcpy(value + (presence & ~QUARRY_C_PRESENCE_COUNT_FLAG), &zero, sizeof(zero));
            if (member->max_count == 0U && member->element.kind == QUARRY_C_TYPED_STRING)
                value[member->value_offset] = 0U;
        } else if (packed) {
            /* Generated presence bits are dense and ordered. Initialize each
             * bitmap byte once, including its unused bits, before any RMW. */
            const size_t byte = (presence & ~QUARRY_C_PRESENCE_PACKED_FLAG) / 8U;
            if (byte != cleared_presence_byte) {
                value[byte] = 0U;
                cleared_presence_byte = byte;
            }
        }
        if (!present) {
            if (!counted) {
                if (!packed) {
                    const bool absent = false;
                    memcpy(value + presence, &absent, sizeof(absent));
                }
                if (member->element.kind != QUARRY_C_TYPED_RECORD)
                    memset(value + member->value_offset, 0, member->element.size);
            }
            continue;
        }
        size_t nested_error = 0U;
        quarry_c_status_t s;
        if (field->storage == QUARRY_C_BRF_V2_STORAGE_FIXED && member->max_count == 0U &&
            member->element.kind <= QUARRY_C_TYPED_ENUM) {
            s = scalar_decode(&member->element, bytes, n, value + member->value_offset);
        } else {
            s = read_field(member, bytes, n, value, &nested_error);
        }
        if (!counted) {
            /* Counted members (arrays, strings, bytes) derive presence from the
             * count/length that read_field stores. Others record it explicitly;
             * presence reads true only once the value decoded successfully. */
            const bool set = s == QUARRY_C_STATUS_OK && member->max_count == 0U &&
                             member->element.kind != QUARRY_C_TYPED_STRING &&
                             member->element.kind != QUARRY_C_TYPED_BYTES;
            if (packed) {
                const size_t bit = presence & ~QUARRY_C_PRESENCE_PACKED_FLAG;
                if (set)
                    value[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
            } else {
                memcpy(value + presence, &set, sizeof(set));
            }
        }
        if (s != QUARRY_C_STATUS_OK) {
            value_status = s;
            value_error = field->byte_offset + nested_error;
        }
    }
    if (tail != record_size) {
        *error = tail;
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    if (value_status != QUARRY_C_STATUS_OK)
        *error = value_error;
    return value_status;
}

quarry_c_typed_decode_result_t quarry_c_typed_decode(const quarry_c_type_descriptor_t* type,
                                                     const uint8_t* input, size_t length,
                                                     uint32_t record_id, void* value) {
    quarry_c_typed_decode_result_t result = {QUARRY_C_STATUS_INVALID_ARGUMENT, false, 0U};
    if (type == NULL || value == NULL)
        return result;
    result.status = read_record(type, input, length, record_id, value, &result.byte_offset);
    result.has_byte_offset = result.status != QUARRY_C_STATUS_OK;
    return result;
}
