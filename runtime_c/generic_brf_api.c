#include "quarry/runtime_c/generic_brf.h"

#include <string.h>

void quarry_workspace_reset(quarry_workspace_t* workspace) {
    if (workspace == NULL)
        return;
    workspace->node_count = 0U;
    workspace->field_state_count = 0U;
    workspace->field_map_count = 0U;
    workspace->child_count = 0U;
    workspace->array_count = 0U;
    workspace->array_element_count = 0U;
    workspace->frame_high_water = 0U;
    if (workspace->records != NULL)
        memset(workspace->records, 0, workspace->record_capacity * sizeof(*workspace->records));
    if (workspace->fields != NULL)
        memset(workspace->fields, 0, workspace->field_capacity * sizeof(*workspace->fields));
    if (workspace->types != NULL)
        memset(workspace->types, 0, workspace->type_capacity * sizeof(*workspace->types));
    if (workspace->enums != NULL)
        memset(workspace->enums, 0, workspace->enum_capacity * sizeof(*workspace->enums));
    if (workspace->enum_values != NULL)
        memset(workspace->enum_values, 0,
               workspace->enum_value_capacity * sizeof(*workspace->enum_values));
    if (workspace->nodes != NULL)
        memset(workspace->nodes, 0, workspace->node_capacity * sizeof(*workspace->nodes));
    if (workspace->field_states != NULL)
        memset(workspace->field_states, 0,
               workspace->field_state_capacity * sizeof(*workspace->field_states));
    if (workspace->field_maps != NULL)
        memset(workspace->field_maps, 0,
               workspace->field_map_capacity * sizeof(*workspace->field_maps));
    if (workspace->children != NULL)
        memset(workspace->children, 0, workspace->child_capacity * sizeof(*workspace->children));
    if (workspace->arrays != NULL)
        memset(workspace->arrays, 0, workspace->array_capacity * sizeof(*workspace->arrays));
    if (workspace->array_elements != NULL)
        memset(workspace->array_elements, 0,
               workspace->array_element_capacity * sizeof(*workspace->array_elements));
    if (workspace->frames != NULL)
        memset(workspace->frames, 0, workspace->frame_capacity * sizeof(*workspace->frames));
}

extern quarry_generic_status_t
quarry_brf_validate_impl(const quarry_qbs_view_t*, const quarry_qbs_record_view_t*, const uint8_t*,
                         size_t, quarry_brf_record_view_t*, const quarry_generic_limits_t*);
extern quarry_generic_status_t
quarry_brf_validate_graph_impl(const quarry_qbs_view_t*, const quarry_qbs_record_view_t*,
                               const uint8_t*, size_t, quarry_brf_record_view_t*,
                               quarry_workspace_t*, const quarry_generic_limits_t*);

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

quarry_generic_status_t quarry_brf_validate_with_workspace(const quarry_qbs_view_t* q,
                                                           const quarry_qbs_record_view_t* s,
                                                           const uint8_t* b, size_t n,
                                                           quarry_brf_record_view_t* out,
                                                           quarry_workspace_t* workspace,
                                                           const quarry_generic_limits_t* limits) {
    return quarry_brf_validate_graph_impl(q, s, b, n, out, workspace, limits);
}
