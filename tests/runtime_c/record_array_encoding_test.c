#include "quarry/runtime_c/generic_brf.h"
#include "quarry/runtime_c/generic_brf_encoding.h"
#include <stdio.h>
#include <stdlib.h>

static int read_file(const char* path, uint8_t** data, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
        return 1;
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0)
        return 1;
    *size = (size_t)length;
    *data = (uint8_t*)malloc(*size);
    if (*data == NULL || fread(*data, 1U, *size, file) != *size)
        return 1;
    fclose(file);
    return 0;
}

static quarry_brf_record_array_provider_t empty_array = {NULL, 0U, NULL};
static quarry_generic_status_t empty_root_field(const quarry_brf_value_provider_t* provider,
                                                uint16_t index, quarry_brf_value_t* out) {
    (void)provider;
    *out = (quarry_brf_value_t){0};
    if (index == 10U) {
        out->kind = QUARRY_BRF_ENCODE_ARRAY;
        out->aggregate = &empty_array;
    }
    return QUARRY_GENERIC_OK;
}

int main(int argc, char** argv) {
    uint8_t* qbs = NULL;
    size_t qbs_size = 0U;
    quarry_qbs_record_view_t records[8];
    quarry_qbs_field_view_t fields[32];
    quarry_qbs_type_view_t types[32];
    quarry_qbs_enum_view_t enums[4];
    uint64_t enum_values[16];
    quarry_brf_record_node_t nodes[8];
    quarry_brf_field_state_t states[32];
    uint32_t maps[32], array_elements[16];
    quarry_brf_child_relation_t children[8];
    quarry_brf_record_array_relation_t arrays[4];
    quarry_brf_validation_frame_t frames[8];
    quarry_workspace_t workspace = {
        records, 8U,  fields, 32U, types,    32U, enums,  4U, enum_values,    16U, nodes,  8U,
        states,  32U, maps,   32U, children, 8U,  arrays, 4U, array_elements, 16U, frames, 8U,
        0U,      0U,  0U,     0U,  0U,       0U,  0U};
    quarry_generic_limits_t limits = {1U << 20U, 1U << 20U, 1024U, 16U, 16U};
    quarry_qbs_view_t schema = {0};
    const quarry_qbs_record_view_t* root = NULL;
    if (argc != 2 || read_file(argv[1], &qbs, &qbs_size) != 0 ||
        quarry_qbs_parse(qbs, qbs_size, &schema, &workspace, &limits) != QUARRY_GENERIC_OK ||
        quarry_qbs_find_record_by_id(&schema, 1U, &root) != QUARRY_GENERIC_OK ||
        root->field_count <= 10U)
        return 1;
    const quarry_qbs_field_view_t* field = &schema.fields[root->field_start + 10U];
    if (field->type_index >= schema.type_count || schema.types[field->type_index].code != 16U)
        return 1;
    const quarry_qbs_type_view_t* array = &schema.types[field->type_index];
    if (array->reference >= schema.type_count || schema.types[array->reference].code != 15U)
        return 1;
    if (schema.types[array->reference].reference >= schema.record_count ||
        schema.records[schema.types[array->reference].reference].record_id != 3U ||
        array->max_elements != 4U)
        return 1;
    quarry_brf_nested_record_plan_t plans[4];
    quarry_brf_nested_frame_t planning_frames[4];
    quarry_brf_nested_field_plan_t planned_fields[32];
    quarry_brf_nested_record_array_plan_t plan_arrays[1];
    quarry_brf_record_array_element_plan_t relationships[1];
    quarry_brf_writer_frame_t writer_frames[4];
    quarry_brf_encoder_workspace_t encoder = {0};
    encoder.nested = (quarry_brf_nested_planning_workspace_t){plans,
                                                              4U,
                                                              planning_frames,
                                                              4U,
                                                              planned_fields,
                                                              32U,
                                                              plan_arrays,
                                                              1U,
                                                              0U,
                                                              0U,
                                                              0U,
                                                              0U,
                                                              relationships,
                                                              1U,
                                                              0U};
    quarry_brf_writer_workspace_t writer = {writer_frames, 4U};
    uint8_t output[256];
    size_t output_size = 0U;
    quarry_generic_status_t smoke =
        quarry_brf_encode(&schema, root, &(quarry_brf_value_provider_t){empty_root_field, NULL},
                          output, sizeof(output), &output_size, &encoder, &writer, NULL);
    if (smoke != QUARRY_GENERIC_OK || output_size == 0U) {
        fprintf(stderr, "smoke=%d size=%zu\n", (int)smoke, output_size);
        return 1;
    }
    free(qbs);
    puts("record array QBS fixture: ok");
    return 0;
}
