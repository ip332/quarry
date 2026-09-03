#include "quarry/runtime_c/generic_brf_encoding.h"

#include <stdio.h>

static quarry_generic_status_t field_value(const quarry_brf_record_provider_t* provider,
                                           uint16_t field, quarry_brf_value_t* out) {
    (void)provider;
    (void)field;
    if (out == NULL)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = (quarry_brf_value_t){QUARRY_BRF_ENCODE_UINT, 7U, 0, false, 0.0F, 0.0, {0}, {0}, NULL};
    return QUARRY_GENERIC_OK;
}

static quarry_generic_status_t record_at(const quarry_brf_record_array_provider_t* provider,
                                         size_t index, const quarry_brf_record_provider_t** out) {
    (void)provider;
    if (out == NULL || index > 1U)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    static const quarry_brf_record_provider_t records[] = {{field_value, NULL},
                                                           {field_value, NULL}};
    *out = &records[index];
    return QUARRY_GENERIC_OK;
}

int main(void) {
    quarry_qbs_record_view_t schema = {0};
    quarry_brf_record_provider_t provider = {field_value, NULL};
    quarry_brf_nested_record_plan_t records[2];
    quarry_brf_nested_frame_t frames[2];
    quarry_brf_nested_field_plan_t fields[2];
    quarry_brf_nested_record_array_plan_t arrays[1];
    quarry_brf_nested_planning_workspace_t workspace = {records, 2U, frames, 2U, fields, 2U,
                                                        arrays,  1U, 0U,     0U, 0U,     0U};
    uint32_t root, child, frame, field, array;
    quarry_brf_record_array_provider_t record_array = {record_at, 2U, NULL};
    const quarry_brf_record_provider_t* element = NULL;
    schema.field_count = 1U;

    if (quarry_brf_nested_plan_push_record(&workspace, &schema, &provider, UINT32_MAX, 0U, &root) !=
            QUARRY_GENERIC_OK ||
        quarry_brf_nested_plan_push_record(&workspace, &schema, &provider, root, 3U, &child) !=
            QUARRY_GENERIC_OK ||
        quarry_brf_nested_plan_push_frame(&workspace, root, &frame) != QUARRY_GENERIC_OK ||
        quarry_brf_nested_plan_add_field(&workspace, root, 0U, &(quarry_brf_value_t){0}, &field) !=
            QUARRY_GENERIC_OK ||
        quarry_brf_nested_plan_add_array(&workspace, root, 4U, record_array.count, &array) !=
            QUARRY_GENERIC_OK ||
        record_array.get_record(&record_array, 1U, &element) != QUARRY_GENERIC_OK ||
        element == NULL || workspace.records[child].parent_record != root ||
        workspace.arrays[array].parent_record != root ||
        workspace.frames[frame].record_plan != root ||
        workspace.fields[field].parent_record != root)
        return 1;
    if (quarry_brf_nested_plan_push_record(&workspace, &schema, &provider, root, 0U, &child) !=
        QUARRY_GENERIC_WORKSPACE_EXHAUSTED)
        return 1;
    quarry_brf_nested_planning_workspace_reset(&workspace);
    if (workspace.record_count != 0U || workspace.frame_count != 0U ||
        workspace.field_count != 0U || workspace.array_count != 0U)
        return 1;
    if (quarry_brf_nested_plan_push_record(&workspace, NULL, &provider, 0U, 0U, &root) !=
        QUARRY_GENERIC_INVALID_ARGUMENT)
        return 1;
    puts("nested planning architecture: ok");
    return 0;
}
