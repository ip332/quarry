#include "quarry/runtime_c/generic_brf.h"

#include <stdio.h>
#include <stdlib.h>

static int load(const char* path, uint8_t** bytes, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) return 1;
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) return 1;
    *size = (size_t)length; *bytes = (uint8_t*)malloc(*size);
    if (*bytes == NULL || fread(*bytes, 1U, *size, file) != *size) return 1;
    fclose(file); return 0;
}

int main(int argc, char** argv) {
    uint8_t* bytes = NULL; size_t size = 0U;
    quarry_qbs_record_view_t records[4]; quarry_qbs_field_view_t fields[8];
    quarry_qbs_type_view_t types[8]; quarry_qbs_enum_view_t enums[1]; uint64_t values[1];
    quarry_workspace_t workspace = {records, 4U, fields, 8U, types, 8U, enums, 1U, values, 1U,
                                    NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U,
                                    0U, 0U, 0U, 0U, 0U, 0U, 0U};
    quarry_qbs_view_t schema = {0};
    const quarry_qbs_record_view_t* parent = NULL;
    if (argc != 2 || load(argv[1], &bytes, &size) != 0 ||
        quarry_qbs_parse(bytes, size, &schema, &workspace, NULL) != QUARRY_GENERIC_OK ||
        quarry_qbs_find_record_by_id(&schema, 1U, &parent) != QUARRY_GENERIC_OK ||
        parent->field_count != 3U)
        return 1;
    const quarry_qbs_field_view_t* field = &schema.fields[parent->field_start + 1U];
    const quarry_qbs_type_view_t* array = &schema.types[field->type_index];
    if (array->code != 16U || array->max_elements != 4U || array->reference >= schema.type_count)
        return 1;
    const quarry_qbs_type_view_t* element = &schema.types[array->reference];
    if (element->code != 15U || element->reference >= schema.record_count ||
        schema.records[element->reference].record_id != 2U || schema.records[element->reference].variable_size != 0U)
        return 1;
    free(bytes); puts("fixed record array fixture: ok"); return 0;
}
