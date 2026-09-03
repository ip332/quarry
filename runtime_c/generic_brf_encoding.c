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
        quarry_brf_nested_planning_workspace_reset(&w->nested);
    }
}

void quarry_brf_nested_planning_workspace_reset(quarry_brf_nested_planning_workspace_t* w) {
    if (w != NULL) {
        w->record_count = 0U;
        w->frame_count = 0U;
        w->field_count = 0U;
        w->array_count = 0U;
    }
}

quarry_generic_status_t quarry_brf_nested_plan_push_record(
    quarry_brf_nested_planning_workspace_t* w, const quarry_qbs_record_view_t* schema,
    const quarry_brf_record_provider_t* provider, uint32_t parent_record, uint16_t parent_field,
    uint32_t* out_index) {
    if (w == NULL || schema == NULL || provider == NULL || provider->get_field == NULL ||
        out_index == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (w->records == NULL || w->record_count >= w->record_capacity || w->fields == NULL ||
        w->field_count > w->field_capacity ||
        schema->field_count > w->field_capacity - w->field_count)
        return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
    if (w->record_count > UINT32_MAX)
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    *out_index = (uint32_t)w->record_count;
    w->records[w->record_count++] = (quarry_brf_nested_record_plan_t){schema,
                                                                      provider,
                                                                      parent_record,
                                                                      parent_field,
                                                                      (uint32_t)w->field_count,
                                                                      schema->field_count,
                                                                      0U,
                                                                      0U};
    w->field_count += schema->field_count;
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_nested_plan_push_frame(quarry_brf_nested_planning_workspace_t* w,
                                                          uint32_t record_plan,
                                                          uint32_t* out_index) {
    if (w == NULL || out_index == NULL || w->frames == NULL || record_plan >= w->record_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (w->frame_count >= w->frame_capacity || w->frame_count > UINT32_MAX)
        return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
    *out_index = (uint32_t)w->frame_count;
    w->frames[w->frame_count++] = (quarry_brf_nested_frame_t){record_plan, 0U, 0U, 0U};
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_nested_plan_add_field(quarry_brf_nested_planning_workspace_t* w,
                                                         uint32_t parent_record,
                                                         uint16_t field_index,
                                                         const quarry_brf_value_t* value,
                                                         uint32_t* out_index) {
    if (w == NULL || value == NULL || out_index == NULL || w->fields == NULL ||
        parent_record >= w->record_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    quarry_brf_nested_record_plan_t* record = &w->records[parent_record];
    if (field_index >= record->field_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out_index = record->first_field + field_index;
    w->fields[*out_index] =
        (quarry_brf_nested_field_plan_t){parent_record,
                                         field_index,
                                         *value,
                                         UINT32_MAX,
                                         UINT32_MAX,
                                         0U,
                                         0U,
                                         0U,
                                         0U,
                                         value->kind != QUARRY_BRF_ENCODE_ABSENT};
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t quarry_brf_nested_plan_add_array(quarry_brf_nested_planning_workspace_t* w,
                                                         uint32_t parent_record,
                                                         uint16_t parent_field, uint32_t count,
                                                         uint32_t* out_index) {
    if (w == NULL || out_index == NULL || w->arrays == NULL || parent_record >= w->record_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    if (w->array_count >= w->array_capacity || w->array_count > UINT32_MAX)
        return QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
    *out_index = (uint32_t)w->array_count;
    w->arrays[w->array_count++] =
        (quarry_brf_nested_record_array_plan_t){parent_record, parent_field, UINT32_MAX, count};
    return QUARRY_GENERIC_OK;
}

typedef struct {
    const quarry_brf_value_provider_t* root;
} nested_root_context_t;

static quarry_generic_status_t nested_root_field(const quarry_brf_record_provider_t* provider,
                                                 uint16_t index, quarry_brf_value_t* out) {
    const nested_root_context_t* c = (const nested_root_context_t*)provider->context;
    return c->root->get_field(c->root, index, out);
}

static bool nested_find_record(const quarry_qbs_view_t* q, const quarry_qbs_record_view_t* r,
                               uint32_t* index) {
    uint32_t i;
    for (i = 0U; i < q->record_count; ++i) {
        if (&q->records[i] == r) {
            *index = i;
            return true;
        }
    }
    return false;
}

static bool nested_scalar_ok(const quarry_qbs_view_t* q, const quarry_qbs_type_view_t* t,
                             const quarry_brf_value_t* v) {
    if (t->code == 1U)
        return v->kind == QUARRY_BRF_ENCODE_BOOL;
    if (t->code == 10U)
        return v->kind == QUARRY_BRF_ENCODE_FLOAT;
    if (t->code == 11U)
        return v->kind == QUARRY_BRF_ENCODE_DOUBLE;
    if (t->code == 12U)
        return v->kind == QUARRY_BRF_ENCODE_ENUM &&
               urange((uint64_t)v->int_value, t->encoded_width) &&
               enum_ok(q, t, (uint64_t)v->int_value);
    if (t->code == 13U)
        return v->kind == QUARRY_BRF_ENCODE_STRING && v->string_value.size <= t->max_bytes &&
               (v->string_value.size == 0U || v->string_value.data != NULL) &&
               utf8((const uint8_t*)v->string_value.data, v->string_value.size);
    if (t->code == 14U)
        return v->kind == QUARRY_BRF_ENCODE_BYTES && v->bytes_value.size <= t->max_bytes &&
               (v->bytes_value.size == 0U || v->bytes_value.data != NULL);
    if (t->code == 2U || t->code == 4U || t->code == 6U || t->code == 8U)
        return v->kind == QUARRY_BRF_ENCODE_INT && irange(v->int_value, t->encoded_width);
    return v->kind == QUARRY_BRF_ENCODE_UINT && urange(v->uint_value, t->encoded_width);
}

static quarry_generic_status_t nested_value_status(const quarry_qbs_view_t* q,
                                                   const quarry_qbs_type_view_t* t,
                                                   const quarry_brf_value_t* v) {
    if (t->code == 13U) {
        if (v->kind != QUARRY_BRF_ENCODE_STRING)
            return QUARRY_GENERIC_TYPE_MISMATCH;
        return v->string_value.size > t->max_bytes ? QUARRY_GENERIC_VALUE_OUT_OF_RANGE
                                                   : QUARRY_GENERIC_INVALID_ARGUMENT;
    }
    if (t->code == 14U) {
        if (v->kind != QUARRY_BRF_ENCODE_BYTES)
            return QUARRY_GENERIC_TYPE_MISMATCH;
        return v->bytes_value.size > t->max_bytes ? QUARRY_GENERIC_VALUE_OUT_OF_RANGE
                                                  : QUARRY_GENERIC_INVALID_ARGUMENT;
    }
    if (t->code == 12U) {
        if (v->kind != QUARRY_BRF_ENCODE_ENUM)
            return QUARRY_GENERIC_TYPE_MISMATCH;
        return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
    }
    if (t->code == 2U || t->code == 4U || t->code == 6U || t->code == 8U) {
        if (v->kind != QUARRY_BRF_ENCODE_INT)
            return QUARRY_GENERIC_TYPE_MISMATCH;
        return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
    }
    if (t->code == 1U || t->code == 10U || t->code == 11U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    if (v->kind != QUARRY_BRF_ENCODE_UINT && t->code != 1U && t->code != 10U && t->code != 11U)
        return QUARRY_GENERIC_TYPE_MISMATCH;
    (void)q;
    return QUARRY_GENERIC_VALUE_OUT_OF_RANGE;
}

static quarry_generic_status_t nested_plan(const quarry_qbs_view_t* q,
                                           const quarry_qbs_record_view_t* root_schema,
                                           const quarry_brf_value_provider_t* root_provider,
                                           quarry_brf_encoder_workspace_t* w, size_t max_work,
                                           size_t max_bytes, size_t* total) {
    nested_root_context_t root_context = {root_provider};
    quarry_brf_record_provider_t root = {nested_root_field, &root_context};
    uint32_t root_index, rp, fp;
    if (!nested_find_record(q, root_schema, &root_index))
        return QUARRY_GENERIC_MALFORMED_QBS;
    quarry_generic_status_t s = quarry_brf_nested_plan_push_record(&w->nested, root_schema, &root,
                                                                   UINT32_MAX, UINT16_MAX, &rp);
    if (s != QUARRY_GENERIC_OK)
        return s;
    s = quarry_brf_nested_plan_push_frame(&w->nested, rp, &fp);
    if (s != QUARRY_GENERIC_OK)
        return s;
    w->nested.frames[fp].destination_offset = 0U;
    w->nested.records[rp].planned_size = 16U + root_schema->fixed_region_size;
    while (w->nested.frame_count != 0U) {
        quarry_brf_nested_frame_t* frame = &w->nested.frames[w->nested.frame_count - 1U];
        quarry_brf_nested_record_plan_t* plan = &w->nested.records[frame->record_plan];
        if (frame->field_cursor == plan->field_count) {
            plan->complete = 1U;
            --w->nested.frame_count;
            if (plan->parent_record != UINT32_MAX) {
                quarry_brf_nested_record_plan_t* parent = &w->nested.records[plan->parent_record];
                quarry_brf_nested_field_plan_t* pf =
                    &w->nested.fields[parent->first_field + plan->parent_field];
                const quarry_qbs_field_view_t* qf =
                    &q->fields[parent->schema->field_start + plan->parent_field];
                if (plan->schema->variable_size) {
                    pf->payload_offset = parent->planned_size;
                    pf->payload_size = plan->planned_size;
                    if (!add_ok(parent->planned_size, plan->planned_size, &parent->planned_size))
                        return QUARRY_GENERIC_RESOURCE_LIMIT;
                } else if (qf->slot_size != plan->planned_size ||
                           plan->schema->complete_fixed_record_size != plan->planned_size)
                    return QUARRY_GENERIC_MALFORMED_QBS;
            }
            continue;
        }
        const uint16_t i = frame->field_cursor++;
        const quarry_qbs_field_view_t* qf = &q->fields[plan->schema->field_start + i];
        quarry_brf_value_t value = {0};
        if (w->work_count >= max_work)
            return QUARRY_GENERIC_RESOURCE_LIMIT;
        ++w->work_count;
        s = plan->provider->get_field(plan->provider, i, &value);
        if (s != QUARRY_GENERIC_OK)
            return s;
        s = quarry_brf_nested_plan_add_field(&w->nested, frame->record_plan, i, &value, &fp);
        if (s != QUARRY_GENERIC_OK)
            return s;
        quarry_brf_nested_field_plan_t* pf = &w->nested.fields[fp];
        if (value.kind == QUARRY_BRF_ENCODE_ABSENT)
            continue;
        if (qf->type_index >= q->type_count)
            return QUARRY_GENERIC_MALFORMED_QBS;
        const quarry_qbs_type_view_t* type = &q->types[qf->type_index];
        if (type->code == 15U) {
            if (value.kind != QUARRY_BRF_ENCODE_RECORD || value.aggregate == NULL ||
                type->reference >= q->record_count)
                return value.kind == QUARRY_BRF_ENCODE_RECORD ? QUARRY_GENERIC_TYPE_MISMATCH
                                                              : QUARRY_GENERIC_TYPE_MISMATCH;
            if ((!q->records[type->reference].variable_size && qf->storage == 2U) ||
                (q->records[type->reference].variable_size &&
                 (qf->storage != 2U || qf->slot_size != 8U)))
                return QUARRY_GENERIC_MALFORMED_QBS;
            const quarry_brf_record_provider_t* child =
                (const quarry_brf_record_provider_t*)value.aggregate;
            uint32_t child_index;
            s = quarry_brf_nested_plan_push_record(&w->nested, &q->records[type->reference], child,
                                                   frame->record_plan, i, &child_index);
            if (s != QUARRY_GENERIC_OK)
                return s;
            pf->child_record = child_index;
            w->nested.records[child_index].planned_size =
                16U + q->records[type->reference].fixed_region_size;
            s = quarry_brf_nested_plan_push_frame(&w->nested, child_index, &fp);
            if (s != QUARRY_GENERIC_OK)
                return s;
            continue;
        }
        if (type->code == 16U) {
            const quarry_brf_array_provider_t* array =
                (const quarry_brf_array_provider_t*)value.aggregate;
            if (array == NULL || array->get_element == NULL || type->reference >= q->type_count)
                return QUARRY_GENERIC_TYPE_MISMATCH;
            const quarry_qbs_type_view_t* element = &q->types[type->reference];
            if (element->code == 13U || element->code == 14U || element->code == 15U ||
                element->code == 16U)
                return QUARRY_GENERIC_UNSUPPORTED_TYPE;
            if (array->count > type->max_elements ||
                w->array_element_count > w->array_element_capacity ||
                array->count > w->array_element_capacity - w->array_element_count)
                return array->count > type->max_elements ? QUARRY_GENERIC_VALUE_OUT_OF_RANGE
                                                         : QUARRY_GENERIC_WORKSPACE_EXHAUSTED;
            size_t count_size;
            if (!varuint_size(array->count, &count_size))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
            pf->array_start = w->array_element_count;
            pf->array_count = array->count;
            pf->payload_offset = plan->planned_size;
            pf->payload_size = count_size;
            w->array_element_count += array->count;
            if (!add_ok(plan->planned_size, count_size, &plan->planned_size))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
            for (size_t j = 0U; j < array->count; ++j) {
                quarry_brf_value_t element_value = {0};
                if (w->work_count >= max_work)
                    return QUARRY_GENERIC_RESOURCE_LIMIT;
                ++w->work_count;
                s = array->get_element(array, j, &element_value);
                if (s != QUARRY_GENERIC_OK)
                    return s;
                if (!nested_scalar_ok(q, element, &element_value))
                    return nested_value_status(q, element, &element_value);
                w->array_elements[pf->array_start + j].value = element_value;
                if (!add_ok(plan->planned_size, element->encoded_width, &plan->planned_size))
                    return QUARRY_GENERIC_RESOURCE_LIMIT;
                if (!add_ok(pf->payload_size, element->encoded_width, &pf->payload_size))
                    return QUARRY_GENERIC_RESOURCE_LIMIT;
            }
            continue;
        }
        if (!nested_scalar_ok(q, type, &value))
            return nested_value_status(q, type, &value);
        size_t n = type->code == 13U   ? value.string_value.size
                   : type->code == 14U ? value.bytes_value.size
                                       : 0U;
        if (type->code == 13U || type->code == 14U) {
            pf->payload_offset = plan->planned_size;
            pf->payload_size = n;
            if (!add_ok(plan->planned_size, n, &plan->planned_size))
                return QUARRY_GENERIC_RESOURCE_LIMIT;
        }
    }
    if (w->nested.records[0].planned_size > max_bytes ||
        w->nested.records[0].planned_size > UINT32_MAX)
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    *total = w->nested.records[0].planned_size;
    (void)root_index;
    return QUARRY_GENERIC_OK;
}

static void nested_write_scalar(uint8_t* dst, const quarry_qbs_field_view_t* field,
                                const quarry_qbs_type_view_t* type,
                                const quarry_brf_value_t* value) {
    if (type->code == 1U) {
        dst[field->byte_offset] = value->bool_value ? 1U : 0U;
    } else if (type->code == 10U || type->code == 11U) {
        uint64_t bits = 0U;
        const size_t n = type->code == 10U ? 4U : 8U;
        memcpy(&bits,
               type->code == 10U ? (const void*)&value->float_value
                                 : (const void*)&value->double_value,
               n);
        putn(dst + field->byte_offset, n, bits);
    } else {
        const uint64_t bits =
            value->kind == QUARRY_BRF_ENCODE_UINT ? value->uint_value : (uint64_t)value->int_value;
        putn(dst + field->byte_offset, field->slot_size, bits);
    }
}

static quarry_generic_status_t nested_write(const quarry_qbs_view_t* q,
                                            quarry_brf_encoder_workspace_t* w, uint8_t* dst,
                                            size_t total) {
    size_t frame_count = 1U;
    w->nested.frames[0] = (quarry_brf_nested_frame_t){0U, 0U, 0U, 1U};
    memset(dst, 0, total);
    while (frame_count != 0U) {
        quarry_brf_nested_frame_t* frame = &w->nested.frames[frame_count - 1U];
        const quarry_brf_nested_record_plan_t* plan = &w->nested.records[frame->record_plan];
        uint8_t* record = dst + frame->destination_offset;
        if (frame->phase == 1U) {
            record[0] = 2U;
            put16(record + 2U, 16U);
            put32(record + 4U, plan->schema->record_id);
            put32(record + 8U, plan->schema->fixed_region_size);
            put32(record + 12U, (uint32_t)plan->planned_size);
            frame->phase = 2U;
        }
        if (frame->field_cursor == plan->field_count) {
            --frame_count;
            continue;
        }
        const uint16_t i = frame->field_cursor++;
        const quarry_qbs_field_view_t* field = &q->fields[plan->schema->field_start + i];
        const quarry_brf_nested_field_plan_t* value = &w->nested.fields[plan->first_field + i];
        if (!value->present)
            continue;
        record[16U + field->presence_bit / 8U] |= (uint8_t)(1U << (field->presence_bit % 8U));
        const quarry_qbs_type_view_t* type = &q->types[field->type_index];
        if (type->code == 15U) {
            const quarry_brf_nested_record_plan_t* child = &w->nested.records[value->child_record];
            const size_t child_offset =
                child->schema->variable_size ? value->payload_offset : field->byte_offset;
            if (child->schema->variable_size) {
                put32(record + field->byte_offset, (uint32_t)value->payload_offset);
                put32(record + field->byte_offset + 4U, (uint32_t)value->payload_size);
            }
            w->nested.frames[frame_count++] =
                (quarry_brf_nested_frame_t){value->child_record, 0U, child_offset, 1U};
        } else if (field->storage == 2U) {
            put32(record + field->byte_offset, (uint32_t)value->payload_offset);
            put32(record + field->byte_offset + 4U, (uint32_t)value->payload_size);
            size_t cursor = 0U;
            put_varuint(record + value->payload_offset, &cursor, value->array_count);
            const quarry_qbs_type_view_t* element = &q->types[type->reference];
            for (size_t j = 0U; j < value->array_count; ++j) {
                const quarry_brf_value_t* item = &w->array_elements[value->array_start + j].value;
                if (element->code == 1U)
                    record[value->payload_offset + cursor++] = item->bool_value ? 1U : 0U;
                else if (element->code == 10U || element->code == 11U) {
                    uint64_t bits = 0U;
                    const size_t n = element->code == 10U ? 4U : 8U;
                    memcpy(&bits,
                           element->code == 10U ? (const void*)&item->float_value
                                                : (const void*)&item->double_value,
                           n);
                    putn(record + value->payload_offset + cursor, n, bits);
                    cursor += n;
                } else {
                    putn(record + value->payload_offset + cursor, element->encoded_width,
                         item->kind == QUARRY_BRF_ENCODE_UINT ? item->uint_value
                                                              : (uint64_t)item->int_value);
                    cursor += element->encoded_width;
                }
            }
        } else if (type->code == 13U || type->code == 14U) {
            const uint8_t* bytes = type->code == 13U
                                       ? (const uint8_t*)value->value.string_value.data
                                       : value->value.bytes_value.data;
            put32(record + field->byte_offset, (uint32_t)value->payload_offset);
            put32(record + field->byte_offset + 4U, (uint32_t)value->payload_size);
            memcpy(record + value->payload_offset, bytes, value->payload_size);
        } else {
            nested_write_scalar(record, field, type, &value->value);
        }
    }
    return QUARRY_GENERIC_OK;
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
        result == NULL || r->field_start > q->field_count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    quarry_brf_encoder_workspace_reset(w);
    bool has_nested = false;
    for (i = 0U; i < r->field_count; ++i) {
        const quarry_qbs_field_view_t* field = &q->fields[r->field_start + i];
        if (field->type_index < q->type_count && q->types[field->type_index].code == 15U)
            has_nested = true;
    }
    if (has_nested && w->nested.records != NULL && w->nested.frames != NULL &&
        w->nested.fields != NULL) {
        const quarry_generic_status_t nested_status = nested_plan(q, r, p, w, maxw, maxb, &total);
        if (nested_status != QUARRY_GENERIC_OK)
            return nested_status;
        if (dst == NULL || cap < total)
            return QUARRY_GENERIC_BUFFER_TOO_SMALL;
        *result = total;
        return nested_write(q, w, dst, total);
    }
    if (w->fields == NULL || r->field_count > w->field_capacity)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
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
                    if (e->code == 12U && !enum_ok(q, e, (uint64_t)ev.int_value))
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
