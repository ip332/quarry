#include "quarry/runtime_c/generic_brf_encoding.h"
#include <limits.h>
#include <string.h>

static bool add_ok(size_t a, size_t b, size_t* out) {
    if (b > SIZE_MAX - a)
        return false;
    *out = a + b;
    return true;
}
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24U);
    p[1] = (uint8_t)(v >> 16U);
    p[2] = (uint8_t)(v >> 8U);
    p[3] = (uint8_t)v;
}
static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8U);
    p[1] = (uint8_t)v;
}
static void putn(uint8_t* p, size_t n, uint64_t v) {
    while (n != 0U) {
        --n;
        p[n] = (uint8_t)v;
        v >>= 8U;
    }
}
static bool varuint_size(size_t v, size_t* out) {
    size_t n = 1U;
    while (v >= 0x80U) {
        v >>= 7U;
        if (!add_ok(n, 1U, &n))
            return false;
    }
    *out = n;
    return true;
}
static void put_varuint(uint8_t* p, size_t* cursor, size_t v) {
    while (v >= 0x80U) {
        p[(*cursor)++] = (uint8_t)((v & 0x7fU) | 0x80U);
        v >>= 7U;
    }
    p[(*cursor)++] = (uint8_t)v;
}
static bool utf8(const uint8_t* p, size_t n) {
    size_t i = 0U;
    while (i < n) {
        uint32_t c = p[i++];
        size_t m = 0U;
        if (c <= 0x7FU)
            continue;
        if (c >= 0xC2U && c <= 0xDFU)
            m = 1U;
        else if (c >= 0xE0U && c <= 0xEFU)
            m = 2U;
        else if (c >= 0xF0U && c <= 0xF4U)
            m = 3U;
        else
            return false;
        if (n - i < m)
            return false;
        while (m-- != 0U)
            if ((p[i++] & 0xC0U) != 0x80U)
                return false;
    }
    return true;
}
static bool urange(uint64_t v, uint16_t n) {
    return n != 0U && n <= 8U && (n == 8U || v <= (UINT64_C(1) << (n * 8U)) - 1U);
}
static bool irange(int64_t v, uint16_t n) {
    unsigned b = (unsigned)n * 8U;
    return n != 0U && n <= 8U &&
           (n == 8U || (v >= -(INT64_C(1) << (b - 1U)) && v <= (INT64_C(1) << (b - 1U)) - 1));
}
static bool enum_ok(const quarry_qbs_view_t* q, const quarry_qbs_type_view_t* t, uint64_t v) {
    uint32_t i;
    if (t->reference >= q->enum_count)
        return false;
    for (i = 0U; i < q->enums[t->reference].value_count; ++i)
        if (q->enum_values[q->enums[t->reference].value_start + i] == v)
            return true;
    return false;
}

void quarry_brf_encoder_workspace_reset(quarry_brf_encoder_workspace_t* w) {
    if (w != NULL) {
        w->field_count = 0U;
        w->work_count = 0U;
        w->array_element_count = 0U;
    }
}

quarry_generic_status_t
quarry_brf_encode(const quarry_qbs_view_t* q, const quarry_qbs_record_view_t* r,
                  const quarry_brf_value_provider_t* p, uint8_t* dst, size_t cap, size_t* result,
                  quarry_brf_encoder_workspace_t* w, const quarry_brf_encode_limits_t* lim) {
    size_t fixed, tail, total;
    uint16_t i;
    size_t maxb = lim == NULL ? SIZE_MAX : lim->max_record_bytes,
           maxw = lim == NULL ? SIZE_MAX : lim->max_work_items;
    if (result != NULL)
        *result = 0U;
    if (q == NULL || r == NULL || p == NULL || p->get_field == NULL || w == NULL ||
        w->fields == NULL || result == NULL || r->field_start > q->field_count ||
        r->field_count > w->field_capacity)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    quarry_brf_encoder_workspace_reset(w);
    if (!add_ok(16U, r->fixed_region_size, &fixed))
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    tail = fixed;
    for (i = 0U; i < r->field_count; ++i) {
        quarry_brf_value_t v = {0};
        const quarry_qbs_field_view_t* f = &q->fields[r->field_start + i];
        const quarry_qbs_type_view_t* t;
        quarry_generic_status_t s;
        if (w->work_count >= maxw)
            return QUARRY_GENERIC_RESOURCE_LIMIT;
        ++w->work_count;
        s = p->get_field(p, i, &v);
        if (s != QUARRY_GENERIC_OK)
            return s;
        w->fields[i] =
            (quarry_brf_encoder_field_t){i, v, 0U, 0U, v.kind != QUARRY_BRF_ENCODE_ABSENT, 0U, 0U};
        ++w->field_count;
        if (!w->fields[i].present)
            continue;
        if (f->type_index >= q->type_count)
            return QUARRY_GENERIC_MALFORMED_QBS;
        t = &q->types[f->type_index];
        if (t->code == 15U)
            return QUARRY_GENERIC_UNSUPPORTED_TYPE;
        if (t->code == 16U) {
            const quarry_brf_array_provider_t* a = (const quarry_brf_array_provider_t*)v.aggregate;
            size_t count_size;
            if (a == NULL || a->get_element == NULL ||
                (a->count != 0U && w->array_elements == NULL))
                return QUARRY_GENERIC_TYPE_MISMATCH;
            if (a->count > t->max_elements || !varuint_size(a->count, &count_size))
                return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
            if (w->array_element_count > w->array_element_capacity ||
                a->count > w->array_element_capacity - w->array_element_count)
                return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
            w->fields[i].array_start = w->array_element_count;
            w->fields[i].array_count = a->count;
            w->array_element_count += a->count;
            w->fields[i].payload_offset = tail;
            if (!add_ok(tail, count_size, &total))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
            tail = total;
            if (t->reference >= q->type_count)
                return QUARRY_GENERIC_MALFORMED_QBS;
            const quarry_qbs_type_view_t* e = &q->types[t->reference];
            /* Variable-width and aggregate elements require the shared value model
             * extensions reserved for a later phase. */
            if (e->code == 13U || e->code == 14U || e->code == 15U || e->code == 16U)
                return QUARRY_GENERIC_UNSUPPORTED_TYPE;
            for (size_t j = 0U; j < a->count; ++j) {
                quarry_brf_value_t ev = {0};
                size_t n = 0U;
                if (w->work_count >= maxw)
                    return QUARRY_GENERIC_RESOURCE_LIMIT;
                ++w->work_count;
                s = a->get_element(a, j, &ev);
                if (s != QUARRY_GENERIC_OK)
                    return s;
                w->array_elements[w->fields[i].array_start + j].value = ev;
                if (e->code == 13U || e->code == 14U) {
                    const uint8_t* data;
                    if (e->code == 13U) {
                        if (ev.kind != QUARRY_BRF_ENCODE_STRING)
                            return QUARRY_GENERIC_TYPE_MISMATCH;
                        data = (const uint8_t*)ev.string_value.data;
                        n = ev.string_value.size;
                        if (n != 0U && data == NULL)
                            return QUARRY_GENERIC_INVALID_ARGUMENT;
                        if (!utf8(data, n))
                            return QUARRY_GENERIC_INVALID_ARGUMENT;
                    } else {
                        if (ev.kind != QUARRY_BRF_ENCODE_BYTES)
                            return QUARRY_GENERIC_TYPE_MISMATCH;
                        data = ev.bytes_value.data;
                        n = ev.bytes_value.size;
                        if (n != 0U && data == NULL)
                            return QUARRY_GENERIC_INVALID_ARGUMENT;
                    }
                    if (n > e->max_bytes || !varuint_size(n, &count_size) ||
                        !add_ok(tail, count_size, &total) || !add_ok(total, n, &tail))
                        return n > e->max_bytes ? QUARRY_GENERIC_VALUE_OUT_OF_RANGE
                                                : QUARRY_GENERIC_RESOURCE_LIMIT;
                    w->array_elements[w->fields[i].array_start + j].payload_offset = total;
                    w->array_elements[w->fields[i].array_start + j].payload_size = n;
                } else {
                    bool valid = false;
                    if (e->code == 1U)
                        valid = ev.kind == QUARRY_BRF_ENCODE_BOOL;
                    else if (e->code == 10U)
                        valid = ev.kind == QUARRY_BRF_ENCODE_FLOAT;
                    else if (e->code == 11U)
                        valid = ev.kind == QUARRY_BRF_ENCODE_DOUBLE;
                    else if (e->code == 12U)
                        valid = ev.kind == QUARRY_BRF_ENCODE_ENUM &&
                                urange((uint64_t)ev.int_value, e->encoded_width);
                    else if (e->code == 2U || e->code == 4U || e->code == 6U || e->code == 8U)
                        valid = ev.kind == QUARRY_BRF_ENCODE_INT &&
                                irange(ev.int_value, e->encoded_width);
                    else
                        valid = ev.kind == QUARRY_BRF_ENCODE_UINT &&
                                urange(ev.uint_value, e->encoded_width);
                    if (!valid)
                        return QUARRY_GENERIC_TYPE_MISMATCH;
                    if (e->code == 12U &&
                        !enum_ok(q, e, (uint64_t)ev.int_value))
                        return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
                    if (!add_ok(tail, e->encoded_width, &tail))
                        return QUARRY_GENERIC_RESOURCE_LIMIT;
                }
            }
            w->fields[i].payload_size = tail - w->fields[i].payload_offset;
            continue;
        }
        if (t->code == 13U || t->code == 14U) {
            size_t n;
            if (t->code == 13U) {
                if (v.kind != QUARRY_BRF_ENCODE_STRING)
                    return QUARRY_GENERIC_TYPE_MISMATCH;
                n = v.string_value.size;
                if ((n != 0U && v.string_value.data == NULL) ||
                    !utf8((const uint8_t*)v.string_value.data, n))
                    return QUARRY_GENERIC_INVALID_ARGUMENT;
            } else {
                if (v.kind != QUARRY_BRF_ENCODE_BYTES)
                    return QUARRY_GENERIC_TYPE_MISMATCH;
                n = v.bytes_value.size;
                if (n != 0U && v.bytes_value.data == NULL)
                    return QUARRY_GENERIC_INVALID_ARGUMENT;
            }
            if (n > t->max_bytes)
                return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
            if (!add_ok(tail, n, &total))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
            w->fields[i].payload_offset = tail;
            w->fields[i].payload_size = n;
            tail = total;
        } else if (t->code == 1U) {
            if (v.kind != QUARRY_BRF_ENCODE_BOOL)
                return QUARRY_GENERIC_TYPE_MISMATCH;
        } else if (t->code == 10U) {
            if (v.kind != QUARRY_BRF_ENCODE_FLOAT)
                return QUARRY_GENERIC_TYPE_MISMATCH;
        } else if (t->code == 11U) {
            if (v.kind != QUARRY_BRF_ENCODE_DOUBLE)
                return QUARRY_GENERIC_TYPE_MISMATCH;
        } else if (t->code == 12U) {
            if (v.kind != QUARRY_BRF_ENCODE_ENUM)
                return QUARRY_GENERIC_TYPE_MISMATCH;
            if (!urange((uint64_t)v.int_value, t->encoded_width) ||
                !enum_ok(q, t, (uint64_t)v.int_value))
                return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
        } else if (t->code == 2U || t->code == 4U || t->code == 6U || t->code == 8U) {
            if (v.kind != QUARRY_BRF_ENCODE_INT)
                return QUARRY_GENERIC_TYPE_MISMATCH;
            if (!irange(v.int_value, t->encoded_width))
                return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
        } else {
            if (v.kind != QUARRY_BRF_ENCODE_UINT)
                return QUARRY_GENERIC_TYPE_MISMATCH;
            if (!urange(v.uint_value, t->encoded_width))
                return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
        }
    }
    total = tail;
    if (total > maxb || total > UINT32_MAX)
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    *result = total;
    if (dst == NULL || cap < total)
        return QUARRY_GENERIC_BUFFER_TOO_SMALL;
    memset(dst, 0, total);
    dst[0] = 2U;
    put16(dst + 2U, 16U);
    put32(dst + 4U, r->record_id);
    put32(dst + 8U, r->fixed_region_size);
    put32(dst + 12U, (uint32_t)total);
    for (i = 0U; i < r->field_count; ++i) {
        const quarry_qbs_field_view_t* f = &q->fields[r->field_start + i];
        const quarry_brf_encoder_field_t* e = &w->fields[i];
        if (!e->present)
            continue;
        dst[16U + f->presence_bit / 8U] |= (uint8_t)(1U << (f->presence_bit % 8U));
        if (f->storage == 2U) {
            const quarry_qbs_type_view_t* t = &q->types[f->type_index];
            if (t->code == 16U) {
                size_t cursor = 0U;
                const quarry_qbs_type_view_t* e = &q->types[t->reference];
                const quarry_brf_encoder_field_t* a = &w->fields[i];
                put32(dst + f->byte_offset, (uint32_t)a->payload_offset);
                put32(dst + f->byte_offset + 4U, (uint32_t)a->payload_size);
                put_varuint(dst + a->payload_offset, &cursor, a->array_count);
                for (size_t j = 0U; j < a->array_count; ++j) {
                    const quarry_brf_value_t* v = &w->array_elements[a->array_start + j].value;
                    if (e->code == 13U || e->code == 14U) {
                        const uint8_t* data = e->code == 13U ? (const uint8_t*)v->string_value.data
                                                             : v->bytes_value.data;
                        const size_t n =
                            e->code == 13U ? v->string_value.size : v->bytes_value.size;
                        put_varuint(dst + a->payload_offset, &cursor, n);
                        memcpy(dst + a->payload_offset + cursor, data, n);
                        cursor += n;
                    } else if (e->code == 1U) {
                        dst[a->payload_offset + cursor++] = v->bool_value ? 1U : 0U;
                    } else if (e->code == 10U || e->code == 11U) {
                        uint64_t bits = 0U;
                        const size_t n = e->code == 10U ? 4U : 8U;
                        memcpy(&bits,
                               e->code == 10U ? (const void*)&v->float_value
                                              : (const void*)&v->double_value,
                               n);
                        putn(dst + a->payload_offset + cursor, n, bits);
                        cursor += n;
                    } else {
                        const uint64_t value =
                            (e->code == 2U || e->code == 4U || e->code == 6U || e->code == 8U)
                                ? (uint64_t)v->int_value
                                : (e->code == 12U ? (uint64_t)v->int_value : v->uint_value);
                        putn(dst + a->payload_offset + cursor, e->encoded_width, value);
                        cursor += e->encoded_width;
                    }
                }
                continue;
            }
            const uint8_t* b = e->value.kind == QUARRY_BRF_ENCODE_STRING
                                   ? (const uint8_t*)e->value.string_value.data
                                   : e->value.bytes_value.data;
            size_t n = e->payload_size;
            put32(dst + f->byte_offset, (uint32_t)e->payload_offset);
            put32(dst + f->byte_offset + 4U, (uint32_t)n);
            memcpy(dst + e->payload_offset, b, n);
        } else if (e->value.kind == QUARRY_BRF_ENCODE_BOOL)
            dst[f->byte_offset] = e->value.bool_value ? 1U : 0U;
        else if (e->value.kind == QUARRY_BRF_ENCODE_UINT || e->value.kind == QUARRY_BRF_ENCODE_ENUM)
            putn(dst + f->byte_offset, f->slot_size,
                 e->value.kind == QUARRY_BRF_ENCODE_UINT ? e->value.uint_value
                                                         : (uint64_t)e->value.int_value);
        else if (e->value.kind == QUARRY_BRF_ENCODE_INT)
            putn(dst + f->byte_offset, f->slot_size, (uint64_t)e->value.int_value);
        else {
            uint64_t bits = 0U;
            size_t n = e->value.kind == QUARRY_BRF_ENCODE_FLOAT ? 4U : 8U;
            memcpy(&bits,
                   e->value.kind == QUARRY_BRF_ENCODE_FLOAT ? (const void*)&e->value.float_value
                                                            : (const void*)&e->value.double_value,
                   n);
            putn(dst + f->byte_offset, n, bits);
        }
    }
    return QUARRY_GENERIC_OK;
}
