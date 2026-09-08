#include "quarry/runtime_c/generic_brf.h"
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
    free(qbs);
    puts("record array QBS fixture: ok");
    return 0;
}
