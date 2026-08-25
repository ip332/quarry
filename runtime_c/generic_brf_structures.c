#include "quarry/runtime_c/generic_brf.h"

#include <string.h>

static uint16_t be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]); }
static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3];
}
static bool span_ok(size_t at, size_t len, size_t total) {
    return at <= total && len <= total - at;
}
static bool mul_ok(size_t a, size_t b, size_t* out) {
    if (a != 0U && b > SIZE_MAX / a)
        return false;
    *out = a * b;
    return true;
}
static bool add_ok(size_t a, size_t b, size_t* out) {
    if (b > SIZE_MAX - a)
        return false;
    *out = a + b;
    return true;
}
static bool varuint(const uint8_t* p, size_t n, size_t* cursor, uint64_t* out) {
    uint64_t value = 0U;
    size_t start = *cursor;
    for (size_t i = 0U; i < 10U && *cursor < n; ++i) {
        const uint8_t byte = p[(*cursor)++];
        if (value > (UINT64_MAX >> 7U))
            return false;
        value = (value << 7U) | (uint64_t)(byte & 0x7fU);
        if ((byte & 0x80U) == 0U) {
            if (i > 0U && p[start] == 0U)
                return false;
            *out = value;
            return true;
        }
    }
    return false;
}
static bool utf8_ok(const uint8_t* p, size_t n) {
    for (size_t i = 0U; i < n;) {
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
            cp = c & 7U;
        } else
            return false;
        if (need > n - i)
            return false;
        for (size_t j = 0U; j < need; ++j) {
            if ((p[i] & 0xc0U) != 0x80U)
                return false;
            cp = (cp << 6U) | (p[i++] & 0x3fU);
        }
        if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU) || (cp <= 0x7ffU && need >= 2U) ||
            (cp <= 0xffffU && need == 3U))
            return false;
    }
    return true;
}
static bool enum_has(const quarry_qbs_view_t* q, const quarry_qbs_type_view_t* t, uint64_t v) {
    if (t->reference >= q->enum_count)
        return false;
    const quarry_qbs_enum_view_t* e = &q->enums[t->reference];
    for (uint32_t i = 0U; i < e->value_count; ++i)
        if (q->enum_values[e->value_start + i] == v)
            return true;
    return false;
}
static quarry_generic_status_t fail_view(quarry_brf_record_view_t* out,
                                         quarry_generic_status_t status) {
    if (out != NULL)
        memset(out, 0, sizeof(*out));
    return status;
}

quarry_generic_status_t
quarry_brf_validate_graph_impl(const quarry_qbs_view_t* q, const quarry_qbs_record_view_t* root,
                               const uint8_t* bytes, size_t size, quarry_brf_record_view_t* out,
                               quarry_workspace_t* w, const quarry_generic_limits_t* limits) {
    if (q == NULL || root == NULL || bytes == NULL || out == NULL || w == NULL || size < 16U)
        return fail_view(out, QUARRY_GENERIC_INVALID_ARGUMENT);
    if (limits != NULL && limits->max_record_bytes != 0U && size > limits->max_record_bytes)
        return fail_view(out, QUARRY_GENERIC_RESOURCE_LIMIT);
    if (w->nodes == NULL || w->field_states == NULL || w->field_maps == NULL || w->frames == NULL ||
        w->node_capacity == 0U || w->frame_capacity == 0U)
        return fail_view(out, QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
    uint32_t root_index = 0U;
    while (root_index < q->record_count && &q->records[root_index] != root)
        ++root_index;
    if (root_index == q->record_count)
        return fail_view(out, QUARRY_GENERIC_INVALID_ARGUMENT);
    size_t node_count = 0U, field_count = 0U, map_count = 0U, child_count = 0U;
    size_t array_count = 0U, element_count = 0U, frame_count = 0U, work = 0U;
    w->node_count = 0U;
    w->field_state_count = 0U;
    w->field_map_count = 0U;
    w->child_count = 0U;
    w->array_count = 0U;
    w->array_element_count = 0U;
    w->frame_high_water = 0U;
    const size_t max_work =
        limits == NULL || limits->max_work_items == 0U ? SIZE_MAX : limits->max_work_items;
    const size_t max_nested =
        limits == NULL || limits->max_nested_records == 0U ? SIZE_MAX : limits->max_nested_records;
    w->nodes[0] = (quarry_brf_record_node_t){0U, root_index, 0U, size, root->field_count, 0U, 0U};
    node_count = 1U;
    w->frames[0] = (quarry_brf_validation_frame_t){0U,         root_index, 0U, size, 0U, 0U,
                                                   UINT32_MAX, UINT32_MAX, 0U, 0U,   0U, 0U,
                                                   0U,         0U,         0U, 0U,   0U};
    frame_count = 1U;
    while (frame_count != 0U) {
        if (frame_count > w->frame_high_water)
            w->frame_high_water = frame_count;
        quarry_brf_validation_frame_t* f = &w->frames[frame_count - 1U];
        const quarry_qbs_record_view_t* rs = &q->records[f->qbs_record_index];
        if (f->phase == 0U) {
            ++work;
            if (work > max_work)
                return fail_view(out, QUARRY_GENERIC_RESOURCE_LIMIT);
            if (f->brf_size < 16U || f->brf_size > size ||
                !span_ok(f->brf_offset, f->brf_size, size) || bytes[f->brf_offset] != 2U ||
                bytes[f->brf_offset + 1U] != 0U || be16(bytes + f->brf_offset + 2U) != 16U ||
                be32(bytes + f->brf_offset + 4U) != rs->record_id ||
                be32(bytes + f->brf_offset + 8U) != rs->fixed_region_size ||
                be32(bytes + f->brf_offset + 12U) != f->brf_size ||
                rs->presence_bitmap_size > rs->fixed_region_size ||
                rs->presence_bitmap_size > f->brf_size - 16U) {
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            }
            if (limits != NULL && limits->max_nested_records != 0U && node_count > max_nested)
                return fail_view(out, QUARRY_GENERIC_RESOURCE_LIMIT);
            w->nodes[f->node_index].field_count = rs->field_count;
            w->nodes[f->node_index].field_map_start = (uint32_t)map_count;
            if (rs->field_count > w->field_map_capacity - map_count)
                return fail_view(out, QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
            f->variable_cursor = 16U + rs->fixed_region_size;
            f->phase = 1U;
        }
        if (f->phase == 1U) {
            for (uint32_t bit = 0U; bit < rs->presence_bitmap_size * 8U; ++bit) {
                bool used = false;
                for (uint16_t i = 0U; i < rs->field_count; ++i) {
                    const quarry_qbs_field_view_t* qf = &q->fields[rs->field_start + i];
                    if (qf->presence_bit == bit)
                        used = true;
                }
                if (!used && (bytes[f->brf_offset + 16U + bit / 8U] & (1U << (bit % 8U))) != 0U)
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            }
            f->phase = 2U;
        }
        if (f->phase == 2U) {
            if (f->field_cursor == rs->field_count) {
                f->phase = 3U;
                continue;
            }
            if (++work > max_work)
                return fail_view(out, QUARRY_GENERIC_RESOURCE_LIMIT);
            const quarry_qbs_field_view_t* qf = &q->fields[rs->field_start + f->field_cursor];
            const quarry_qbs_type_view_t* t = &q->types[qf->type_index];
            if (!span_ok(qf->byte_offset, qf->slot_size, f->brf_size) ||
                qf->presence_bit / 8U >= rs->presence_bitmap_size)
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            const bool present = (bytes[f->brf_offset + 16U + qf->presence_bit / 8U] &
                                  (1U << (qf->presence_bit % 8U))) != 0U;
            const size_t fixed = f->brf_offset + qf->byte_offset;
            if (!present) {
                for (size_t z = 0U; z < qf->slot_size; ++z)
                    if (bytes[fixed + z] != 0U)
                        return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                w->field_maps[map_count++] = (uint32_t)field_count;
                w->field_states[field_count++] = (quarry_brf_field_state_t){
                    qf->field_index, 0U,        qf->byte_offset, qf->slot_size, 0U, 0U,
                    UINT32_MAX,      UINT32_MAX};
                ++f->field_cursor;
                continue;
            }
            size_t value_off = qf->byte_offset, value_len = qf->slot_size;
            if (qf->storage == 2U) {
                if (qf->slot_size != 8U || be32(bytes + fixed) != f->variable_cursor ||
                    !span_ok(be32(bytes + fixed), be32(bytes + fixed + 4U), f->brf_size)) {
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                }
                value_off = be32(bytes + fixed);
                value_len = be32(bytes + fixed + 4U);
                f->variable_cursor += value_len;
            }
            if (!span_ok(value_off, value_len, f->brf_size))
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            if (t->code == 1U && (value_len != 1U || bytes[f->brf_offset + value_off] > 1U))
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            if (t->code == 13U && value_len > t->max_bytes)
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            if (t->code == 12U) {
                uint64_t v = 0U;
                for (size_t z = 0U; z < value_len; ++z)
                    v = (v << 8U) | bytes[f->brf_offset + value_off + z];
                if (!enum_has(q, t, v))
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            }
            if (t->code == 13U && !utf8_ok(bytes + f->brf_offset + value_off, value_len))
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            if (t->code == 16U) {
                size_t c = 0U;
                uint64_t count = 0U;
                if (!varuint(bytes + f->brf_offset + value_off, value_len, &c, &count) ||
                    count > t->max_elements)
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                if (limits != NULL && limits->max_array_elements != 0U &&
                    count > limits->max_array_elements)
                    return fail_view(out, QUARRY_GENERIC_RESOURCE_LIMIT);
                if (array_count >= w->array_capacity)
                    return fail_view(out, QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
                w->arrays[array_count] = (quarry_brf_record_array_relation_t){
                    f->node_index, qf->field_index, (uint32_t)element_count, (uint32_t)count};
                if (t->reference >= q->type_count)
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                const quarry_qbs_type_view_t* et = &q->types[t->reference];
                if (et->code == 15U) {
                    const quarry_qbs_record_view_t* child_schema = &q->records[et->reference];
                    for (uint64_t k = 0U; k < count; ++k) {
                        size_t child_len = child_schema->complete_fixed_record_size;
                        if (child_schema->variable_size) {
                            uint64_t nlen = 0U;
                            if (!varuint(bytes + f->brf_offset + value_off, value_len, &c, &nlen) ||
                                nlen == 0U)
                                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                            child_len = (size_t)nlen;
                        }
                        if (!span_ok(c, child_len, value_len) ||
                            element_count >= w->array_element_capacity ||
                            node_count >= w->node_capacity || frame_count >= w->frame_capacity)
                            return fail_view(out, QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
                        size_t child_rel = c;
                        c += child_len;
                        w->array_elements[element_count++] = (uint32_t)node_count;
                        w->nodes[node_count] =
                            (quarry_brf_record_node_t){(uint32_t)node_count,
                                                       et->reference,
                                                       f->brf_offset + value_off + child_rel,
                                                       child_len,
                                                       child_schema->field_count,
                                                       0U,
                                                       0U};
                        ++node_count;
                        /* Elements are validated by frames before the parent resumes. */
                        w->frames[frame_count++] =
                            (quarry_brf_validation_frame_t){(uint32_t)(node_count - 1U),
                                                            et->reference,
                                                            f->brf_offset + value_off + child_rel,
                                                            child_len,
                                                            0U,
                                                            0U,
                                                            UINT32_MAX,
                                                            UINT32_MAX,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U,
                                                            0U};
                    }
                    if (c != value_len)
                        return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                } else {
                    size_t width = et->encoded_width;
                    if (et->code == 1U && width != 1U)
                        return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                    size_t bytes_needed = 0U;
                    if (!mul_ok((size_t)count, width, &bytes_needed) ||
                        bytes_needed != value_len - c)
                        return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                    for (uint64_t k = 0U; k < count; ++k) {
                        const uint8_t* ep =
                            bytes + f->brf_offset + value_off + c + (size_t)k * width;
                        if (et->code == 1U && ep[0] > 1U)
                            return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                        if (et->code == 12U) {
                            uint64_t v = 0U;
                            for (size_t z = 0U; z < width; ++z)
                                v = (v << 8U) | ep[z];
                            if (!enum_has(q, et, v))
                                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                        }
                    }
                }
                w->field_maps[map_count++] = (uint32_t)field_count;
                w->field_states[field_count++] = (quarry_brf_field_state_t){
                    qf->field_index, 1U,        qf->byte_offset, qf->slot_size,
                    value_off,       value_len, UINT32_MAX,      (uint32_t)array_count};
                ++array_count;
                ++f->field_cursor;
                continue;
            }
            if (t->code == 15U) {
                if (t->reference >= q->record_count || child_count >= w->child_capacity ||
                    node_count >= w->node_capacity || frame_count >= w->frame_capacity)
                    return fail_view(out, QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
                const quarry_qbs_record_view_t* cs = &q->records[t->reference];
                size_t child_len = cs->variable_size ? value_len : cs->complete_fixed_record_size;
                if ((!cs->variable_size &&
                     (qf->storage == 2U || qf->slot_size != child_len || value_len != child_len)) ||
                    child_len == 0U || !span_ok(value_off, child_len, f->brf_size))
                    return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
                w->children[child_count] = (quarry_brf_child_relation_t){
                    f->node_index, qf->field_index, (uint32_t)node_count, f->brf_offset + value_off,
                    child_len};
                w->nodes[node_count] = (quarry_brf_record_node_t){(uint32_t)node_count,
                                                                  t->reference,
                                                                  f->brf_offset + value_off,
                                                                  child_len,
                                                                  cs->field_count,
                                                                  0U,
                                                                  0U};
                w->field_maps[map_count++] = (uint32_t)field_count;
                w->field_states[field_count++] = (quarry_brf_field_state_t){
                    qf->field_index, 1U,        qf->byte_offset,       qf->slot_size,
                    value_off,       value_len, (uint32_t)child_count, UINT32_MAX};
                ++child_count;
                ++node_count;
                ++f->field_cursor;
                w->frames[frame_count++] =
                    (quarry_brf_validation_frame_t){(uint32_t)(node_count - 1U),
                                                    t->reference,
                                                    f->brf_offset + value_off,
                                                    child_len,
                                                    0U,
                                                    0U,
                                                    UINT32_MAX,
                                                    UINT32_MAX,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U,
                                                    0U};
                continue;
            }
            w->field_maps[map_count++] = (uint32_t)field_count;
            w->field_states[field_count++] = (quarry_brf_field_state_t){
                qf->field_index, 1U,        qf->byte_offset, qf->slot_size,
                value_off,       value_len, UINT32_MAX,      UINT32_MAX};
            ++f->field_cursor;
        }
        if (f->phase == 3U) {
            if (f->variable_cursor != f->brf_size)
                return fail_view(out, QUARRY_GENERIC_MALFORMED_BRF);
            w->nodes[f->node_index].complete = 1U;
            --frame_count;
        }
    }
    out->qbs = q;
    out->schema = root;
    out->bytes = bytes;
    out->root_bytes = bytes;
    out->size = size;
    out->fixed_end = 16U + root->fixed_region_size;
    out->tail = size;
    out->workspace = w;
    out->node_index = 0U;
    w->node_count = node_count;
    w->field_state_count = field_count;
    w->field_map_count = map_count;
    w->child_count = child_count;
    w->array_count = array_count;
    w->array_element_count = element_count;
    return QUARRY_GENERIC_OK;
}

static quarry_generic_status_t array_value(const quarry_brf_record_view_t* r, uint16_t index,
                                           quarry_brf_array_view_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (r == NULL || r->qbs == NULL || r->schema == NULL || r->bytes == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_field_view_t* f = NULL;
    quarry_generic_status_t s = quarry_qbs_record_field(r->qbs, r->schema, index, &f);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (f->presence_bit / 8U >= r->schema->presence_bitmap_size)
        return QUARRY_GENERIC_MALFORMED_BRF;
    if ((r->bytes[16U + f->presence_bit / 8U] & (1U << (f->presence_bit % 8U))) == 0U)
        return QUARRY_GENERIC_FIELD_ABSENT;
    const quarry_qbs_type_view_t* t = &r->qbs->types[f->type_index];
    if (t->code != 16U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    if (f->storage != 2U || f->slot_size != 8U || !span_ok(f->byte_offset, 8U, r->size))
        return QUARRY_GENERIC_MALFORMED_BRF;
    const size_t off = be32(r->bytes + f->byte_offset);
    const size_t len = be32(r->bytes + f->byte_offset + 4U);
    if (!span_ok(off, len, r->size))
        return QUARRY_GENERIC_MALFORMED_BRF;
    size_t cursor = 0U;
    uint64_t count = 0U;
    if (!varuint(r->bytes + off, len, &cursor, &count) || count > t->max_elements)
        return QUARRY_GENERIC_MALFORMED_BRF;
    if (t->reference >= r->qbs->type_count)
        return QUARRY_GENERIC_MALFORMED_QBS;
    out->element_type = t->reference;
    out->element_code = r->qbs->types[t->reference].code;
    out->count = (uint32_t)count;
    out->payload_offset = off + cursor;
    out->payload_size = len - cursor;
    out->relation_index = UINT32_MAX;
    if (r->workspace != NULL) {
        if (r->node_index < r->workspace->node_capacity) {
            const quarry_brf_record_node_t* node = &r->workspace->nodes[r->node_index];
            for (uint16_t i = 0U; i < node->field_count; ++i) {
                const size_t map = (size_t)node->field_map_start + i;
                if (map >= r->workspace->field_map_capacity)
                    break;
                const uint32_t state_index = r->workspace->field_maps[map];
                if (state_index >= r->workspace->field_state_capacity)
                    break;
                const quarry_brf_field_state_t* state = &r->workspace->field_states[state_index];
                if (state->field_index == index && state->array_relation != UINT32_MAX) {
                    out->relation_index = state->array_relation;
                    break;
                }
            }
        }
    }
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_get_array(const quarry_brf_record_view_t* r, uint16_t index,
                                             quarry_brf_array_view_t* out) {
    return array_value(r, index, out);
}

static quarry_generic_status_t array_element(const quarry_brf_record_view_t* r,
                                             const quarry_brf_array_view_t* a, size_t index,
                                             const uint8_t** p, size_t* width) {
    if (r == NULL || a == NULL || p == NULL || width == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (index >= a->count || a->element_type >= r->qbs->type_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_type_view_t* t = &r->qbs->types[a->element_type];
    size_t offset = a->payload_offset;
    if (t->code == 13U || t->code == 14U || t->code == 15U) {
        size_t cursor = 0U;
        uint64_t len = 0U;
        for (size_t i = 0U; i <= index; ++i) {
            if (!varuint(r->bytes + a->payload_offset, a->payload_size, &cursor, &len) ||
                len > a->payload_size - cursor)
                return QUARRY_GENERIC_MALFORMED_BRF;
            offset = a->payload_offset + cursor;
            cursor += (size_t)len;
        }
        *p = r->bytes + offset;
        *width = (size_t)len;
        return QUARRY_GENERIC_OK;
    }
    if (!mul_ok(index, t->encoded_width, &offset) || !add_ok(offset, a->payload_offset, &offset) ||
        !span_ok(offset, t->encoded_width, r->size))
        return QUARRY_GENERIC_MALFORMED_BRF;
    *p = r->bytes + offset;
    *width = t->encoded_width;
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_array_get_uint(const quarry_brf_record_view_t* r,
                                                  const quarry_brf_array_view_t* a, size_t i,
                                                  uint64_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = 0U;
    if (a == NULL || (a->element_code != 3U && a->element_code != 5U && a->element_code != 7U &&
                      a->element_code != 9U))
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    for (size_t j = 0U; j < n; ++j)
        *out = (*out << 8U) | p[j];
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_array_get_int(const quarry_brf_record_view_t* r,
                                                 const quarry_brf_array_view_t* a, size_t i,
                                                 int64_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = 0;
    if (a == NULL || (a->element_code != 2U && a->element_code != 4U && a->element_code != 6U &&
                      a->element_code != 8U))
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint64_t v = 0U;
    for (size_t j = 0U; j < n; ++j)
        v = (v << 8U) | p[j];
    if (n < 8U && (v & (UINT64_C(1) << (n * 8U - 1U))) != 0U)
        v |= UINT64_MAX << (n * 8U);
    *out = (int64_t)v;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_array_get_bool(const quarry_brf_record_view_t* r,
                                                  const quarry_brf_array_view_t* a, size_t i,
                                                  bool* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = false;
    if (a == NULL || a->element_code != 1U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    *out = p[0] != 0U;
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_array_get_float(const quarry_brf_record_view_t* r,
                                                   const quarry_brf_array_view_t* a, size_t i,
                                                   float* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = 0.0F;
    if (a == NULL || a->element_code != 10U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint32_t v = be32(p);
    memcpy(out, &v, 4U);
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_array_get_double(const quarry_brf_record_view_t* r,
                                                    const quarry_brf_array_view_t* a, size_t i,
                                                    double* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = 0.0;
    if (a == NULL || a->element_code != 11U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint64_t v = ((uint64_t)be32(p) << 32U) | be32(p + 4U);
    memcpy(out, &v, 8U);
    return QUARRY_GENERIC_OK;
}
quarry_generic_status_t quarry_brf_array_get_enum(const quarry_brf_record_view_t* r,
                                                  const quarry_brf_array_view_t* a, size_t i,
                                                  int64_t* out) {
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = 0;
    if (a == NULL || a->element_code != 12U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    const uint8_t* p;
    size_t n;
    quarry_generic_status_t s = array_element(r, a, i, &p, &n);
    if (s != QUARRY_GENERIC_OK)
        return s;
    uint64_t v = 0U;
    for (size_t j = 0U; j < n; ++j)
        v = (v << 8U) | p[j];
    if (!enum_has(r->qbs, &r->qbs->types[a->element_type], v))
        return QUARRY_GENERIC_MALFORMED_BRF;
    *out = (int64_t)v;
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_get_record(const quarry_brf_record_view_t* r, uint16_t index,
                                              quarry_brf_record_view_t* out) {
    if (out != NULL)
        memset(out, 0, sizeof(*out));
    if (r == NULL || out == NULL || r->workspace == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_qbs_field_view_t* f;
    quarry_generic_status_t s = quarry_qbs_record_field(r->qbs, r->schema, index, &f);
    if (s != QUARRY_GENERIC_OK)
        return s;
    if (f->presence_bit / 8U >= r->schema->presence_bitmap_size ||
        (r->bytes[16U + f->presence_bit / 8U] & (1U << (f->presence_bit % 8U))) == 0U)
        return QUARRY_GENERIC_FIELD_ABSENT;
    for (size_t i = 0U; i < r->workspace->child_capacity; ++i) {
        const quarry_brf_child_relation_t* c = &r->workspace->children[i];
        if (c->parent_node == r->node_index && c->parent_field == index) {
            if (c->child_node >= r->workspace->node_capacity)
                return QUARRY_GENERIC_MALFORMED_BRF;
            const quarry_brf_record_node_t* n = &r->workspace->nodes[c->child_node];
            if (!n->complete)
                return QUARRY_GENERIC_MALFORMED_BRF;
            out->qbs = r->qbs;
            out->schema = &r->qbs->records[n->qbs_record_index];
            out->bytes = r->root_bytes + c->brf_offset;
            out->root_bytes = r->root_bytes;
            out->size = c->brf_size;
            out->fixed_end = 16U + out->schema->fixed_region_size;
            out->tail = out->size;
            out->workspace = r->workspace;
            out->node_index = c->child_node;
            return QUARRY_GENERIC_OK;
        }
    }
    return QUARRY_GENERIC_MALFORMED_BRF;
}

quarry_generic_status_t quarry_brf_get_record_array(const quarry_brf_record_view_t* r,
                                                    uint16_t index, quarry_brf_array_view_t* out) {
    return array_value(r, index, out);
}
quarry_generic_status_t quarry_brf_record_array_get(const quarry_brf_record_view_t* r,
                                                    const quarry_brf_array_view_t* a, size_t index,
                                                    quarry_brf_record_view_t* out) {
    if (out != NULL)
        memset(out, 0, sizeof(*out));
    if (r == NULL || a == NULL || out == NULL || r->workspace == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (index >= a->count || a->relation_index >= r->workspace->array_capacity)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const quarry_brf_record_array_relation_t* relation = &r->workspace->arrays[a->relation_index];
    if (index >= relation->count ||
        relation->element_start > r->workspace->array_element_capacity - index)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    const uint32_t node = r->workspace->array_elements[relation->element_start + index];
    if (node >= r->workspace->node_capacity || !r->workspace->nodes[node].complete)
        return QUARRY_GENERIC_MALFORMED_BRF;
    const quarry_brf_record_node_t* n = &r->workspace->nodes[node];
    out->qbs = r->qbs;
    out->schema = &r->qbs->records[n->qbs_record_index];
    out->bytes = r->root_bytes + n->brf_offset;
    out->root_bytes = r->root_bytes;
    out->size = n->brf_size;
    out->fixed_end = 16U + out->schema->fixed_region_size;
    out->tail = out->size;
    out->workspace = r->workspace;
    out->node_index = node;
    return QUARRY_GENERIC_OK;
}
