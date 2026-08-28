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
            (quarry_brf_encoder_field_t){i, v, 0U, 0U, v.kind != QUARRY_BRF_ENCODE_ABSENT};
        ++w->field_count;
        if (!w->fields[i].present)
            continue;
        if (f->type_index >= q->type_count)
            return QUARRY_GENERIC_MALFORMED_QBS;
        t = &q->types[f->type_index];
        if (t->code == 15U || t->code == 16U)
            return QUARRY_GENERIC_UNSUPPORTED_TYPE;
        if (t->code == 13U || t->code == 14U) {
            size_t n = t->code == 13U ? v.string_value.size : v.bytes_value.size;
            if ((t->code == 13U && (v.kind != QUARRY_BRF_ENCODE_STRING ||
                                    !utf8((const uint8_t*)v.string_value.data, n))) ||
                (t->code == 14U && v.kind != QUARRY_BRF_ENCODE_BYTES) || n > t->max_bytes)
                return QUARRY_GENERIC_INVALID_ARGUMENT;
            if (!add_ok(tail, n, &total))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
            w->fields[i].payload_offset = tail;
            w->fields[i].payload_size = n;
            tail = total;
        } else if (t->code == 1U    ? v.kind != QUARRY_BRF_ENCODE_BOOL
                   : t->code == 10U ? v.kind != QUARRY_BRF_ENCODE_FLOAT
                   : t->code == 11U ? v.kind != QUARRY_BRF_ENCODE_DOUBLE
                   : t->code == 12U ? (v.kind != QUARRY_BRF_ENCODE_ENUM ||
                                       !urange((uint64_t)v.int_value, t->encoded_width) ||
                                       !enum_ok(q, t, (uint64_t)v.int_value))
                   : (t->code == 2U || t->code == 4U || t->code == 6U || t->code == 8U)
                       ? (v.kind != QUARRY_BRF_ENCODE_INT || !irange(v.int_value, t->encoded_width))
                       : (v.kind != QUARRY_BRF_ENCODE_UINT ||
                          !urange(v.uint_value, t->encoded_width)))
            return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
    }
    total = tail;
    if (total > maxb)
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
