#include "quarry/runtime_c/generic_brf.h"

extern quarry_generic_status_t
quarry_brf_validate_impl(const quarry_qbs_view_t*, const quarry_qbs_record_view_t*, const uint8_t*,
                         size_t, quarry_brf_record_view_t*, const quarry_generic_limits_t*);

quarry_generic_status_t quarry_brf_validate(const quarry_qbs_view_t* q,
                                            const quarry_qbs_record_view_t* s, const uint8_t* b,
                                            size_t n, quarry_brf_record_view_t* out,
                                            const quarry_generic_limits_t* limits) {
    if (q == NULL || s == NULL || b == NULL || out == NULL || n < 16U)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (s->presence_bitmap_size > 4U)
        return QUARRY_GENERIC_MALFORMED_QBS;
    uint32_t used = 0U;
    for (uint16_t i = 0U; i < s->field_count; ++i) {
        const quarry_qbs_field_view_t* f = &q->fields[s->field_start + i];
        if (f->presence_bit >= s->presence_bitmap_size * 8U)
            return QUARRY_GENERIC_MALFORMED_QBS;
        used |= (uint32_t)1U << f->presence_bit;
    }
    for (uint32_t bit = 0U; bit < s->presence_bitmap_size * 8U; ++bit)
        if ((b[16U + bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U &&
            (used & ((uint32_t)1U << bit)) == 0U)
            return QUARRY_GENERIC_MALFORMED_BRF;
    for (uint16_t i = 0U; i < s->field_count; ++i) {
        const quarry_qbs_field_view_t* f = &q->fields[s->field_start + i];
        if ((b[16U + f->presence_bit / 8U] & (uint8_t)(1U << (f->presence_bit % 8U))) == 0U)
            continue;
        const quarry_qbs_type_view_t* t = &q->types[f->type_index];
        if (t->code == 1U && b[f->byte_offset] > 1U)
            return QUARRY_GENERIC_MALFORMED_BRF;
        if (t->code != 12U)
            continue;
        uint64_t value = 0U;
        for (uint32_t j = 0U; j < t->encoded_width; ++j)
            value = (value << 8U) | b[f->byte_offset + j];
        if (t->reference >= q->enum_count)
            return QUARRY_GENERIC_MALFORMED_QBS;
        const quarry_qbs_enum_view_t* e = &q->enums[t->reference];
        bool found = false;
        for (uint32_t j = 0U; j < e->value_count; ++j)
            if (q->enum_values[e->value_start + j] == value)
                found = true;
        if (!found)
            return QUARRY_GENERIC_MALFORMED_BRF;
    }
    return quarry_brf_validate_impl(q, s, b, n, out, limits);
}
