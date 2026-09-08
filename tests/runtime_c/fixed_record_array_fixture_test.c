#include "quarry/runtime_c/generic_brf.h"
#include "quarry/runtime_c/generic_brf_encoding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t value; size_t calls; } item_context_t;
typedef struct { quarry_brf_record_provider_t* items; size_t count, calls; } array_context_t;
typedef struct { const quarry_brf_record_array_provider_t* array; uint8_t before, after; size_t calls; } parent_context_t;
static quarry_generic_status_t item_field(const quarry_brf_record_provider_t* p, uint16_t i, quarry_brf_value_t* out) {
    item_context_t* c = (item_context_t*)p->context; ++c->calls;
    if (i != 0U) return QUARRY_GENERIC_FIELD_NOT_FOUND;
    *out = (quarry_brf_value_t){.kind = QUARRY_BRF_ENCODE_UINT, .uint_value = c->value}; return QUARRY_GENERIC_OK;
}
static quarry_generic_status_t array_item(const quarry_brf_record_array_provider_t* p, size_t i, const quarry_brf_record_provider_t** out) {
    array_context_t* c = (array_context_t*)p->context; ++c->calls;
    if (i >= c->count) return QUARRY_GENERIC_FIELD_NOT_FOUND;
    *out = &c->items[i]; return QUARRY_GENERIC_OK;
}
static quarry_generic_status_t parent_field(const quarry_brf_value_provider_t* p, uint16_t i, quarry_brf_value_t* out) {
    const parent_context_t* c = (const parent_context_t*)p->context; *out = (quarry_brf_value_t){0};
    if (i == 0U) { out->kind = QUARRY_BRF_ENCODE_UINT; out->uint_value = c->before; }
    else if (i == 1U) { out->kind = QUARRY_BRF_ENCODE_ARRAY; out->aggregate = c->array; }
    else if (i == 2U) { out->kind = QUARRY_BRF_ENCODE_UINT; out->uint_value = c->after; }
    else return QUARRY_GENERIC_FIELD_NOT_FOUND;
    return QUARRY_GENERIC_OK;
}

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
    item_context_t item_values[4] = {{0x11223344U, 0U}, {0x55667788U, 0U}, {0x99aabbccU, 0U}, {0xddeeff00U, 0U}};
    quarry_brf_record_provider_t item_providers[4];
    for (size_t i = 0U; i < 4U; ++i) item_providers[i] = (quarry_brf_record_provider_t){item_field, &item_values[i]};
    array_context_t array_context = {item_providers, 3U, 0U};
    quarry_brf_record_array_provider_t array_provider = {array_item, 3U, &array_context};
    parent_context_t parent_context = {&array_provider, 0xa1U, 0xb2U, 0U};
    quarry_brf_value_provider_t provider = {parent_field, &parent_context};
    quarry_brf_nested_record_plan_t plans[16]; quarry_brf_nested_frame_t plan_frames[8];
    quarry_brf_nested_field_plan_t plan_fields[32]; quarry_brf_nested_record_array_plan_t plan_arrays[4];
    quarry_brf_record_array_element_plan_t relationships[8]; quarry_brf_writer_frame_t writer_frames[8];
    quarry_brf_encoder_workspace_t encoder = {0};
    encoder.nested = (quarry_brf_nested_planning_workspace_t){.records = plans, .record_capacity = 16U,
        .frames = plan_frames, .frame_capacity = 8U, .fields = plan_fields, .field_capacity = 32U,
        .arrays = plan_arrays, .array_capacity = 4U, .array_elements = relationships, .array_element_capacity = 8U};
    quarry_brf_writer_workspace_t writer = {writer_frames, 8U};
    uint8_t output[256]; size_t output_size = 0U;
    quarry_generic_status_t encode_status = quarry_brf_encode(&schema, parent, &provider, output, sizeof(output), &output_size, &encoder, &writer, NULL);
    if (encode_status != QUARRY_GENERIC_OK) return 1;
    static const uint8_t expected_three[] = {
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x5b,
        0x07,0xa1,0x00,0x00,0x00,0x1b,0x00,0x00,0x00,0x40,0xb2,0x03,
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x15,0x01,0x11,0x22,0x33,0x44,
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x15,0x01,0x55,0x66,0x77,0x88,
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x15,0x01,0x99,0xaa,0xbb,0xcc};
    if (output_size != sizeof(expected_three) || memcmp(output, expected_three, sizeof(expected_three)) != 0 ||
        array_context.calls != 3U || item_values[0].calls != 1U || item_values[1].calls != 1U || item_values[2].calls != 1U) {
        return 1;
    }
    array_context.count = array_provider.count = 4U;
    if (quarry_brf_encode(&schema, parent, &provider, output, sizeof(output), &output_size, &encoder, &writer, NULL) != QUARRY_GENERIC_OK)
        return 1;
    array_context.count = array_provider.count = 5U; memset(output, 0x5a, sizeof(output));
    if (quarry_brf_encode(&schema, parent, &provider, output, sizeof(output), &output_size, &encoder, &writer, NULL) != QUARRY_GENERIC_VALUE_OUT_OF_RANGE ||
        output[0] != 0x5aU || output[255] != 0x5aU)
        return 1;
    free(bytes); puts("fixed record array fixture: ok"); return 0;
}
