#include "quarry/runtime_c/generic_brf.h"

#include <string.h>

#define quarry_brf_validate quarry_brf_validate_impl

static uint16_t u16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]); }
static uint32_t u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3];
}
static uint64_t u64(const uint8_t* p) { return ((uint64_t)u32(p) << 32U) | u32(p + 4U); }
static uint32_t uw(const uint8_t* p, uint8_t width) {
    uint32_t v = 0U;
    for (uint8_t i = 0U; i < width; ++i)
        v = (v << 8U) | p[i];
    return v;
}
static bool range(size_t at, size_t length, size_t total) {
    return at <= total && length <= total - at;
}
static bool add(size_t a, size_t b, size_t* out) {
    if (b > SIZE_MAX - a)
        return false;
    *out = a + b;
    return true;
}

static bool utf8(const uint8_t* p, size_t n) {
    size_t i = 0U;
    while (i < n) {
        uint8_t c = p[i++];
        size_t need = 0U;
        uint32_t cp = c;
        if (c < 0x80U)
            continue;
        if (c >= 0xc2U && c <= 0xdfU) {
            need = 1U;
            cp = c & 0x1fU;
        } else if (c >= 0xe0U && c <= 0xefU) {
            need = 2U;
            cp = c & 0x0fU;
        } else if (c >= 0xf0U && c <= 0xf4U) {
            need = 3U;
            cp = c & 0x07U;
        } else
            return false;
        if (i + need > n)
            return false;
        while (need-- > 0U) {
            if ((p[i] & 0xc0U) != 0x80U)
                return false;
            cp = (cp << 6U) | (p[i++] & 0x3fU);
        }
        if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU) ||
            (cp <= 0x7ffU && i >= 2U && (p[i - 2U] & 0xe0U) == 0xe0U) ||
            (cp <= 0xffffU && i >= 3U && (p[i - 3U] & 0xf0U) == 0xf0U))
            return false;
    }
    return true;
}

static quarry_generic_status_t section(const uint8_t* b, size_t n, uint16_t kind, size_t* at,
                                       size_t* size) {
    if (n < 40U || u16(b + 28U) > 64U)
        return QUARRY_GENERIC_MALFORMED_QBS;
    uint16_t count = u16(b + 28U);
    uint32_t dir = u32(b + 32U);
    if (!range(dir, (size_t)count * 12U, n))
        return QUARRY_GENERIC_MALFORMED_QBS;
    for (uint16_t i = 0U; i < count; ++i) {
        const uint8_t* e = b + dir + (size_t)i * 12U;
        if (u16(e) == kind) {
            *at = u32(e + 4U);
            *size = u32(e + 8U);
            return range(*at, *size, n) ? QUARRY_GENERIC_OK : QUARRY_GENERIC_MALFORMED_QBS;
        }
    }
    return QUARRY_GENERIC_MALFORMED_QBS;
}

quarry_generic_status_t quarry_qbs_parse(const uint8_t* b, size_t n, quarry_qbs_view_t* out,
                                         quarry_workspace_t* w,
                                         const quarry_generic_limits_t* lim) {
    if (b == NULL || out == NULL || w == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (lim != NULL && n > lim->max_image_size)
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    if (n < 40U)
        return QUARRY_GENERIC_MALFORMED_QBS;
    if (memcmp(b, "QBS\0", 4U) != 0 || b[4] != 1U || b[5] != 0U || u16(b + 6U) != 40U ||
        b[8] != 1U || u16(b + 10U) != 16U || u16(b + 30U) != 0U || u32(b + 36U) != n)
        return QUARRY_GENERIC_MALFORMED_QBS;
    size_t ro, rs, fo, fs, to, ts, eo, es, vo, vs, io, is, so, ss;
    if (section(b, n, 1U, &ro, &rs) != QUARRY_GENERIC_OK ||
        section(b, n, 2U, &fo, &fs) != QUARRY_GENERIC_OK ||
        section(b, n, 3U, &to, &ts) != QUARRY_GENERIC_OK ||
        section(b, n, 6U, &io, &is) != QUARRY_GENERIC_OK)
        return QUARRY_GENERIC_MALFORMED_QBS;
    bool has_enum = section(b, n, 4U, &eo, &es) == QUARRY_GENERIC_OK;
    bool has_values = section(b, n, 5U, &vo, &vs) == QUARRY_GENERIC_OK;
    bool has_strings = section(b, n, 7U, &so, &ss) == QUARRY_GENERIC_OK;
    size_t record_stride = 28U + b[9];
    if ((has_enum != has_values) || rs % record_stride != 0U || fs % 28U != 0U || ts % 16U != 0U ||
        (has_enum && es % (16U + b[9]) != 0U))
        return QUARRY_GENERIC_MALFORMED_QBS;
    size_t rc = rs / record_stride, fc = fs / 28U, tc = ts / 16U,
           ec = has_enum ? es / (16U + b[9]) : 0U;
    if (rc > w->record_capacity || fc > w->field_capacity || tc > w->type_capacity ||
        ec > w->enum_capacity)
        return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
    out->bytes = b;
    out->size = n;
    out->records = w->records;
    out->record_count = rc;
    out->fields = w->fields;
    out->field_count = fc;
    out->types = w->types;
    out->type_count = tc;
    out->enums = w->enums;
    out->enum_count = ec;
    out->enum_values = w->enum_values;
    out->enum_value_count = 0U;
    out->iss_offset = (uint32_t)io;
    out->iss_size = (uint32_t)is;
    out->strings_offset = has_strings ? (uint32_t)so : 0U;
    out->strings_size = has_strings ? (uint32_t)ss : 0U;
    out->identity_width = b[9];
    for (size_t i = 0U; i < tc; ++i) {
        const uint8_t* p = b + to + i * 16U;
        uint8_t flags = p[1];
        if ((flags & 3U) != 1U && (flags & 3U) != 2U)
            return QUARRY_GENERIC_MALFORMED_QBS;
        w->types[i] = (quarry_qbs_type_view_t){p[0],        (uint8_t)((flags & 1U) != 0U),
                                               u16(p + 2U), u16(p + 4U),
                                               u32(p + 8U), u32(p + 12U)};
        if (p[0] < 1U || p[0] > 16U)
            return QUARRY_GENERIC_MALFORMED_QBS;
    }
    for (size_t i = 0U; i < fc; ++i) {
        const uint8_t* p = b + fo + i * 28U;
        w->fields[i] = (quarry_qbs_field_view_t){u16(p),
                                                 u16(p + 14U),
                                                 u32(p + 4U),
                                                 u32(p + 10U),
                                                 u16(p + 16U),
                                                 u32(p + 20U),
                                                 (uint8_t)(u16(p + 2U) & 3U),
                                                 (uint8_t)((u16(p + 2U) >> 2U) & 1U),
                                                 u16(p + 24U)};
        if (u16(p + 18U) != 0U || u16(p + 26U) != 0U || w->fields[i].type_index >= tc)
            return QUARRY_GENERIC_MALFORMED_QBS;
    }
    for (size_t i = 0U; i < rc; ++i) {
        const uint8_t* p = b + ro + i * record_stride;
        w->records[i] = (quarry_qbs_record_view_t){u32(p),
                                                   u32(p + 4U),
                                                   u16(p + 8U),
                                                   (uint8_t)(u16(p + 10U) & 1U),
                                                   u32(p + 12U),
                                                   u32(p + 16U),
                                                   u32(p + 20U),
                                                   uw(p + 24U, b[9]),
                                                   u16(p + 24U + b[9])};
        if ((size_t)w->records[i].field_start + w->records[i].field_count > fc ||
            w->records[i].presence_bitmap_size > w->records[i].fixed_region_size)
            return QUARRY_GENERIC_MALFORMED_QBS;
    }
    if (has_enum) {
        size_t value_count = vs / 8U;
        if (value_count > w->enum_value_capacity)
            return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
        for (size_t i = 0U; i < ec; ++i) {
            const uint8_t* p = b + eo + i * (16U + b[9]);
            w->enums[i] = (quarry_qbs_enum_view_t){u16(p), u32(p + 4U), u32(p + 8U), u32(p + 12U),
                                                   u16(p + 12U + b[9])};
            if ((size_t)w->enums[i].value_start + w->enums[i].value_count > value_count)
                return QUARRY_GENERIC_MALFORMED_QBS;
        }
        for (size_t i = 0U; i < value_count; ++i)
            w->enum_values[i] = u64(b + vo + i * 8U);
        out->enum_value_count = value_count;
    }
    (void)so;
    (void)ss;
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_qbs_find_record_by_id(const quarry_qbs_view_t* q, uint32_t id,
                                                     const quarry_qbs_record_view_t** out) {
    if (q == NULL || out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    for (size_t i = 0; i < q->record_count; ++i)
        if (q->records[i].record_id == id) {
            *out = &q->records[i];
            return QUARRY_GENERIC_OK;
        }
    return QUARRY_GENERIC_FIELD_NOT_FOUND;
}
quarry_generic_status_t quarry_qbs_find_record_by_name(const quarry_qbs_view_t* q, const char* name,
                                                       size_t len,
                                                       const quarry_qbs_record_view_t** out) {
    if (q == NULL || name == NULL || out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    for (size_t i = 0; i < q->record_count; ++i) {
        const quarry_qbs_record_view_t* r = &q->records[i];
        size_t p = q->iss_offset + r->identity_offset;
        if (p < q->iss_offset + q->iss_size && strlen(name) == len &&
            memcmp(q->bytes + p, name, len) == 0) {
            *out = r;
            return QUARRY_GENERIC_OK;
        }
    }
    return QUARRY_GENERIC_FIELD_NOT_FOUND;
}
quarry_generic_status_t quarry_qbs_record_field(const quarry_qbs_view_t* q,
                                                const quarry_qbs_record_view_t* r, uint16_t idx,
                                                const quarry_qbs_field_view_t** out) {
    if (q == NULL || r == NULL || out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (idx >= r->field_count)
        return QUARRY_GENERIC_FIELD_NOT_FOUND;
    const quarry_qbs_field_view_t* f = &q->fields[r->field_start + idx];
    if (f->field_index != idx)
        return QUARRY_GENERIC_MALFORMED_QBS;
    *out = f;
    return QUARRY_GENERIC_OK;
}

static quarry_generic_status_t field_value(const quarry_brf_record_view_t* r, uint16_t idx,
                                           const quarry_qbs_field_view_t** f, bool* present,
                                           const uint8_t** p, size_t* n) {
    if (r == NULL || r->qbs == NULL || f == NULL || present == NULL || p == NULL || n == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    quarry_generic_status_t s = quarry_qbs_record_field(r->qbs, r->schema, idx, f);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if ((*f)->presence_bit >= r->schema->presence_bitmap_size * 8U)
        return QUARRY_GENERIC_MALFORMED_BRF;
    *present = (r->bytes[16U + (*f)->presence_bit / 8U] &
                (uint8_t)(1U << ((*f)->presence_bit % 8U))) != 0U;
    if (!*present)
        return QUARRY_GENERIC_FIELD_ABSENT;
    if (!range((*f)->byte_offset, (*f)->slot_size, r->size))
        return QUARRY_GENERIC_MALFORMED_BRF;
    *p = r->bytes + (*f)->byte_offset;
    *n = (*f)->slot_size;
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_validate(const quarry_qbs_view_t* q,
                                            const quarry_qbs_record_view_t* s, const uint8_t* b,
                                            size_t n, quarry_brf_record_view_t* out,
                                            const quarry_generic_limits_t* lim) {
    if (q == NULL || s == NULL || b == NULL || out == NULL || n < 16U)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (lim != NULL && (n > lim->max_record_bytes))
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    if (b[0] != 2U || b[1] != 0U || u16(b + 2U) != 16U || u32(b + 4U) != s->record_id ||
        u32(b + 8U) != s->fixed_region_size || u32(b + 12U) != n || s->fixed_region_size > n - 16U)
        return QUARRY_GENERIC_MALFORMED_BRF;
    size_t tail = 16U + s->fixed_region_size;
    for (uint16_t i = 0U; i < s->field_count; ++i) {
        const quarry_qbs_field_view_t* f = &q->fields[s->field_start + i];
        bool present =
            (b[16U + f->presence_bit / 8U] & (uint8_t)(1U << (f->presence_bit % 8U))) != 0U;
        if (!present) {
            if (!range(f->byte_offset, f->slot_size, n)) {
                return QUARRY_GENERIC_MALFORMED_BRF;
            }
            for (size_t z = 0U; z < f->slot_size; ++z)
                if (b[f->byte_offset + z] != 0U)
                    return QUARRY_GENERIC_MALFORMED_BRF;
            continue;
        }
        const quarry_qbs_type_view_t* t = &q->types[f->type_index];
        if (t->code == 15U || t->code == 16U)
            return QUARRY_GENERIC_UNSUPPORTED_TYPE;
        if (f->storage == 2U) {
            if (f->slot_size != 8U || !range(f->byte_offset, 8U, n) ||
                u32(b + f->byte_offset) != tail)
                return QUARRY_GENERIC_MALFORMED_BRF;
            uint32_t off = u32(b + f->byte_offset), len = u32(b + f->byte_offset + 4U);
            if (!range(off, len, n) || len > t->max_bytes)
                return QUARRY_GENERIC_MALFORMED_BRF;
            if (t->code == 13U && !utf8(b + off, len))
                return QUARRY_GENERIC_MALFORMED_BRF;
            tail += (size_t)len;
        } else if (t->code == 13U || t->code == 14U)
            return QUARRY_GENERIC_MALFORMED_BRF;
    }
    if (tail != n)
        return QUARRY_GENERIC_MALFORMED_BRF;
    out->qbs = q;
    out->schema = s;
    out->bytes = b;
    out->size = n;
    out->fixed_end = 16U + s->fixed_region_size;
    out->tail = tail;
    return QUARRY_GENERIC_OK;
}

static quarry_generic_status_t scalar(const quarry_brf_record_view_t* r, uint16_t i, uint8_t code,
                                      const uint8_t** p, size_t* n) {
    const quarry_qbs_field_view_t* f;
    bool present;
    quarry_generic_status_t s = field_value(r, i, &f, &present, p, n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (r->qbs->types[f->type_index].code != code)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_field_is_present(const quarry_brf_record_view_t* r, uint16_t i,
                                                    bool* out) {
    if (r == NULL || out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_field_view_t* f;
    quarry_generic_status_t s = quarry_qbs_record_field(r->qbs, r->schema, i, &f);
    if (s != QUARRY_GENERIC_OK)
        return s;
    *out = (r->bytes[16U + f->presence_bit / 8U] & (uint8_t)(1U << (f->presence_bit % 8U))) != 0U;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_uint(const quarry_brf_record_view_t* r, uint16_t i,
                                            uint64_t* out) {
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = scalar(r, i, 0U, &p, &n);
    if (s == QUARRY_GENERIC_TYPE_MISMATCH) {
        const quarry_qbs_field_view_t* f;
        bool x;
        s = field_value(r, i, &f, &x, &p, &n);
        if (s != QUARRY_GENERIC_OK)
            return s;
        if (r->qbs->types[f->type_index].code != 3U && r->qbs->types[f->type_index].code != 5U &&
            r->qbs->types[f->type_index].code != 7U && r->qbs->types[f->type_index].code != 9U)
            return QUARRY_GENERIC_TYPE_MISMATCH;
    }
    if (s != QUARRY_GENERIC_OK)
        return s;
    *out = 0U;
    for (size_t j = 0; j < n; ++j)
        *out = (*out << 8U) | p[j];
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_int(const quarry_brf_record_view_t* r, uint16_t i,
                                           int64_t* out) {
    const uint8_t* p;
    size_t n;
    const quarry_qbs_field_view_t* f;
    bool x;
    quarry_generic_status_t s = field_value(r, i, &f, &x, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint8_t c = r->qbs->types[f->type_index].code;
    if (c != 2U && c != 4U && c != 6U && c != 8U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    uint64_t u = 0U;
    for (size_t j = 0; j < n; ++j)
        u = (u << 8U) | p[j];
    if (n < 8U && (u & (UINT64_C(1) << (n * 8U - 1U))) != 0U)
        u |= (~UINT64_C(0)) << (n * 8U);
    *out = (int64_t)u;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_bool(const quarry_brf_record_view_t* r, uint16_t i,
                                            bool* out) {
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = scalar(r, i, 1U, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    *out = p[0] != 0U;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_float(const quarry_brf_record_view_t* r, uint16_t i,
                                             float* out) {
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = scalar(r, i, 10U, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint32_t u = u32(p);
    memcpy(out, &u, 4U);
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_double(const quarry_brf_record_view_t* r, uint16_t i,
                                              double* out) {
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = scalar(r, i, 11U, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint64_t u = u64(p);
    memcpy(out, &u, 8U);
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_enum(const quarry_brf_record_view_t* r, uint16_t i,
                                            int64_t* out) {
    const uint8_t* p;
    size_t n;
    const quarry_qbs_field_view_t* f;
    bool present;
    quarry_generic_status_t s = field_value(r, i, &f, &present, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (r->qbs->types[f->type_index].code != 12U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    uint64_t u = 0U;
    for (size_t j = 0; j < n; ++j)
        u = (u << 8U) | p[j];
    const quarry_qbs_enum_view_t* e = &r->qbs->enums[r->qbs->types[f->type_index].reference];
    bool found = false;
    for (uint32_t k = 0U; k < e->value_count; ++k)
        if (r->qbs->enum_values[e->value_start + k] == u)
            found = true;
    if (!found)
        return QUARRY_GENERIC_MALFORMED_BRF;
    *out = (int64_t)u;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_string(const quarry_brf_record_view_t* r, uint16_t i,
                                              quarry_string_view_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_field_view_t* f;
    bool x;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = field_value(r, i, &f, &x, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (r->qbs->types[f->type_index].code != 13U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    uint32_t off = u32(p), len = u32(p + 4U);
    if (!range(off, len, r->size) || len > r->qbs->types[f->type_index].max_bytes ||
        !utf8(r->bytes + off, len))
        return QUARRY_GENERIC_MALFORMED_BRF;
    out->data = (const char*)(r->bytes + off);
    out->size = len;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_get_bytes(const quarry_brf_record_view_t* r, uint16_t i,
                                             quarry_bytes_view_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_field_view_t* f;
    bool x;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = field_value(r, i, &f, &x, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (r->qbs->types[f->type_index].code != 14U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    uint32_t off = u32(p), len = u32(p + 4U);
    if (!range(off, len, r->size) || len > r->qbs->types[f->type_index].max_bytes)
        return QUARRY_GENERIC_MALFORMED_BRF;
    out->data = r->bytes + off;
    out->size = len;
    return QUARRY_GENERIC_OK;
}
