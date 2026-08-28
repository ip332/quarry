#include "quarry/runtime_c/generic_brf.h"

#include <string.h>

enum { TRAVERSAL_RECORD = 0U, TRAVERSAL_ARRAY = 1U };

void quarry_brf_traversal_workspace_reset(quarry_brf_traversal_workspace_t* workspace) {
    if (workspace != NULL) {
        workspace->frame_high_water = 0U;
        workspace->work_count = 0U;
    }
}

static quarry_brf_traversal_result_t emit(const quarry_brf_traversal_callback_t callback,
                                          void* context,
                                          quarry_brf_traversal_workspace_t* workspace,
                                          size_t max_work, quarry_brf_traversal_event_t* event) {
    if (workspace->work_count >= max_work)
        return QUARRY_BRF_TRAVERSAL_WORK_LIMIT;
    ++workspace->work_count;
    return callback(event, context) == QUARRY_BRF_TRAVERSAL_STOP ? QUARRY_BRF_TRAVERSAL_STOPPED
                                                                 : QUARRY_BRF_TRAVERSAL_COMPLETED;
}

static quarry_generic_status_t scalar(const quarry_brf_record_view_t* record, uint16_t field,
                                      uint8_t code, quarry_brf_scalar_t* out) {
    memset(out, 0, sizeof(*out));
    if (code == 1U) {
        out->kind = QUARRY_BRF_SCALAR_BOOL;
        return quarry_brf_get_bool(record, field, &out->bool_value);
    }
    if (code == 2U || code == 4U || code == 6U || code == 8U) {
        out->kind = QUARRY_BRF_SCALAR_INT;
        return quarry_brf_get_int(record, field, &out->int_value);
    }
    if (code == 3U || code == 5U || code == 7U || code == 9U) {
        out->kind = QUARRY_BRF_SCALAR_UINT;
        return quarry_brf_get_uint(record, field, &out->uint_value);
    }
    if (code == 10U) {
        out->kind = QUARRY_BRF_SCALAR_FLOAT;
        return quarry_brf_get_float(record, field, &out->float_value);
    }
    if (code == 11U) {
        out->kind = QUARRY_BRF_SCALAR_DOUBLE;
        return quarry_brf_get_double(record, field, &out->double_value);
    }
    if (code == 12U) {
        out->kind = QUARRY_BRF_SCALAR_ENUM;
        return quarry_brf_get_enum(record, field, &out->int_value);
    }
    if (code == 13U) {
        out->kind = QUARRY_BRF_SCALAR_STRING;
        return quarry_brf_get_string(record, field, &out->string_value);
    }
    if (code == 14U) {
        out->kind = QUARRY_BRF_SCALAR_BYTES;
        return quarry_brf_get_bytes(record, field, &out->bytes_value);
    }
    return QUARRY_GENERIC_TYPE_MISMATCH;
}

static quarry_generic_status_t array_scalar(const quarry_brf_record_view_t* record,
                                            const quarry_brf_array_view_t* array, size_t index,
                                            uint8_t code, quarry_brf_scalar_t* out) {
    memset(out, 0, sizeof(*out));
    if (code == 1U) {
        out->kind = QUARRY_BRF_SCALAR_BOOL;
        return quarry_brf_array_get_bool(record, array, index, &out->bool_value);
    }
    if (code == 12U) {
        out->kind = QUARRY_BRF_SCALAR_ENUM;
        return quarry_brf_array_get_enum(record, array, index, &out->int_value);
    }
    if (code == 10U) {
        out->kind = QUARRY_BRF_SCALAR_FLOAT;
        return quarry_brf_array_get_float(record, array, index, &out->float_value);
    }
    if (code == 11U) {
        out->kind = QUARRY_BRF_SCALAR_DOUBLE;
        return quarry_brf_array_get_double(record, array, index, &out->double_value);
    }
    if (code == 2U || code == 4U || code == 6U || code == 8U) {
        out->kind = QUARRY_BRF_SCALAR_INT;
        return quarry_brf_array_get_int(record, array, index, &out->int_value);
    }
    if (code == 13U) {
        out->kind = QUARRY_BRF_SCALAR_STRING;
        return quarry_brf_array_get_string(record, array, index, &out->string_value);
    }
    if (code == 14U) {
        out->kind = QUARRY_BRF_SCALAR_BYTES;
        return quarry_brf_array_get_bytes(record, array, index, &out->bytes_value);
    }
    out->kind = QUARRY_BRF_SCALAR_UINT;
    return quarry_brf_array_get_uint(record, array, index, &out->uint_value);
}

quarry_brf_traversal_result_t quarry_brf_traverse(const quarry_brf_record_view_t* root,
                                                  quarry_brf_traversal_callback_t callback,
                                                  void* context,
                                                  quarry_brf_traversal_workspace_t* workspace,
                                                  const quarry_brf_traversal_limits_t* limits) {
    const size_t max_work = limits == NULL ? (size_t)1U << 20U : limits->max_work_items;
    const size_t max_depth = limits == NULL ? 1024U : limits->max_depth;
    if (root == NULL || callback == NULL || workspace == NULL || workspace->frames == NULL ||
        workspace->frame_capacity == 0U)
        return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
    quarry_brf_traversal_workspace_reset(workspace);
    workspace->frames[0] =
        (quarry_brf_traversal_frame_t){*root, {0}, 0U, 0U, 0U, TRAVERSAL_RECORD, 0U};
    size_t frame_count = 1U;
    while (frame_count != 0U) {
        if (frame_count > workspace->frame_high_water)
            workspace->frame_high_water = frame_count;
        quarry_brf_traversal_frame_t* frame = &workspace->frames[frame_count - 1U];
        quarry_brf_traversal_event_t event = {0};
        event.depth = frame->depth;
        if (frame->kind == TRAVERSAL_RECORD) {
            if (frame->began == 0U) {
                frame->began = 1U;
                event.kind = QUARRY_BRF_EVENT_RECORD_BEGIN;
                event.record = frame->record;
                quarry_brf_traversal_result_t result =
                    emit(callback, context, workspace, max_work, &event);
                if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                    return result;
                continue;
            }
            if (frame->next >= frame->record.schema->field_count) {
                event.kind = QUARRY_BRF_EVENT_RECORD_END;
                event.record = frame->record;
                quarry_brf_traversal_result_t result =
                    emit(callback, context, workspace, max_work, &event);
                if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                    return result;
                --frame_count;
                continue;
            }
            const uint16_t field_index = (uint16_t)frame->next++;
            const quarry_qbs_field_view_t* field = NULL;
            if (quarry_qbs_record_field(frame->record.qbs, frame->record.schema, field_index,
                                        &field) != QUARRY_GENERIC_OK)
                return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
            bool present = false;
            if (quarry_brf_field_is_present(&frame->record, field_index, &present) !=
                QUARRY_GENERIC_OK)
                return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
            event.kind = QUARRY_BRF_EVENT_FIELD;
            event.field_index = field_index;
            event.present = present;
            event.record = frame->record;
            quarry_brf_traversal_result_t result =
                emit(callback, context, workspace, max_work, &event);
            if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                return result;
            if (!present)
                continue;
            const uint8_t code = frame->record.qbs->types[field->type_index].code;
            if (code == 15U) {
                quarry_brf_record_view_t child;
                if (quarry_brf_get_record(&frame->record, field_index, &child) != QUARRY_GENERIC_OK)
                    return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
                if (frame->depth >= max_depth)
                    return QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT;
                if (frame_count >= workspace->frame_capacity)
                    return QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED;
                workspace->frames[frame_count++] = (quarry_brf_traversal_frame_t){
                    child, {0}, 0U, 0U, frame->depth + 1U, TRAVERSAL_RECORD, 0U};
                continue;
            }
            if (code == 16U) {
                quarry_brf_array_view_t array;
                if (quarry_brf_get_array(&frame->record, field_index, &array) != QUARRY_GENERIC_OK)
                    return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
                event.kind = QUARRY_BRF_EVENT_ARRAY_BEGIN;
                event.array = array;
                event.record = frame->record;
                result = emit(callback, context, workspace, max_work, &event);
                if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                    return result;
                if (frame_count >= workspace->frame_capacity)
                    return QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED;
                workspace->frames[frame_count++] = (quarry_brf_traversal_frame_t){
                    frame->record, array, field_index, 0U, frame->depth, TRAVERSAL_ARRAY, 1U};
                continue;
            }
            event.kind = QUARRY_BRF_EVENT_SCALAR;
            if (scalar(&frame->record, field_index, code, &event.scalar) != QUARRY_GENERIC_OK)
                return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
            result = emit(callback, context, workspace, max_work, &event);
            if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                return result;
            continue;
        }
        if (frame->next >= frame->array.count) {
            event.kind = QUARRY_BRF_EVENT_ARRAY_END;
            event.field_index = frame->field_index;
            event.present = true;
            event.array = frame->array;
            quarry_brf_traversal_result_t result =
                emit(callback, context, workspace, max_work, &event);
            if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                return result;
            --frame_count;
            continue;
        }
        const size_t index = frame->next++;
        event.kind = QUARRY_BRF_EVENT_ARRAY_ELEMENT;
        event.field_index = frame->field_index;
        event.present = true;
        event.index = index;
        event.array = frame->array;
        quarry_brf_traversal_result_t result = emit(callback, context, workspace, max_work, &event);
        if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
            return result;
        if (frame->array.element_code == 15U) {
            quarry_brf_record_view_t child;
            if (quarry_brf_record_array_get(&frame->record, &frame->array, index, &child) !=
                QUARRY_GENERIC_OK)
                return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
            if (frame->depth >= max_depth)
                return QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT;
            if (frame_count >= workspace->frame_capacity)
                return QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED;
            workspace->frames[frame_count++] = (quarry_brf_traversal_frame_t){
                child, {0}, 0U, 0U, frame->depth + 1U, TRAVERSAL_RECORD, 0U};
        } else {
            event.kind = QUARRY_BRF_EVENT_SCALAR;
            if (array_scalar(&frame->record, &frame->array, index, frame->array.element_code,
                             &event.scalar) != QUARRY_GENERIC_OK)
                return QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT;
            result = emit(callback, context, workspace, max_work, &event);
            if (result != QUARRY_BRF_TRAVERSAL_COMPLETED)
                return result;
        }
    }
    return QUARRY_BRF_TRAVERSAL_COMPLETED;
}
