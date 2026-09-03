#include "quarry/runtime_c/generic_brf.h"
#include "quarry/runtime_c/generic_brf_encoding.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const quarry_brf_record_provider_t* child;
} root_context_t;
static int child_failure;

static quarry_generic_status_t child_field(const quarry_brf_record_provider_t* provider,
                                           uint16_t index, quarry_brf_value_t* out) {
    (void)provider;
    if (child_failure)
        return QUARRY_GENERIC_RESOURCE_LIMIT;
    *out = (quarry_brf_value_t){0};
    if (index == 0U) {
        out->kind = QUARRY_BRF_ENCODE_UINT;
        out->uint_value = 100U;
    } else if (index == 1U) {
        static const char text[] = "child";
        out->kind = QUARRY_BRF_ENCODE_STRING;
        out->string_value = (quarry_string_view_t){text, sizeof(text) - 1U};
    } else {
        out->kind = QUARRY_BRF_ENCODE_ABSENT;
    }
    return QUARRY_GENERIC_OK;
}

static quarry_generic_status_t root_field(const quarry_brf_value_provider_t* provider,
                                          uint16_t index, quarry_brf_value_t* out) {
    const root_context_t* context = (const root_context_t*)provider->context;
    *out = (quarry_brf_value_t){0};
    if (index == 9U) {
        out->kind = QUARRY_BRF_ENCODE_RECORD;
        out->aggregate = context->child;
    } else {
        out->kind = QUARRY_BRF_ENCODE_ABSENT;
    }
    return QUARRY_GENERIC_OK;
}

static int load(const char* path, uint8_t** data, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
        return 1;
    length = ftell(file);
    if (length <= 0L || fseek(file, 0L, SEEK_SET) != 0)
        return 1;
    *size = (size_t)length;
    *data = (uint8_t*)malloc(*size);
    if (*data == NULL || fread(*data, 1U, *size, file) != *size)
        return 1;
    fclose(file);
    return 0;
}

int main(int argc, char** argv) {
    uint8_t *qbs = NULL, *output = NULL;
    size_t qbs_size = 0U, output_size = 0U;
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
    quarry_brf_validation_frame_t validation_frames[8];
    quarry_workspace_t parse_workspace = {records,
                                          8U,
                                          fields,
                                          32U,
                                          types,
                                          32U,
                                          enums,
                                          4U,
                                          enum_values,
                                          16U,
                                          nodes,
                                          8U,
                                          states,
                                          32U,
                                          maps,
                                          32U,
                                          children,
                                          8U,
                                          arrays,
                                          4U,
                                          array_elements,
                                          16U,
                                          validation_frames,
                                          8U,
                                          0U,
                                          0U,
                                          0U,
                                          0U,
                                          0U,
                                          0U,
                                          0U};
    quarry_generic_limits_t limits = {1U << 20U, 1U << 20U, 1U << 20U, 16U, 16U};
    quarry_qbs_view_t schema = {0};
    const quarry_qbs_record_view_t* parent = NULL;
    quarry_brf_encoder_field_t root_fields[13];
    quarry_brf_encoder_array_element_t encoded_elements[16];
    quarry_brf_nested_record_plan_t plans[4];
    quarry_brf_nested_frame_t frames[4];
    quarry_brf_nested_field_plan_t planned_fields[32];
    quarry_brf_nested_record_array_plan_t record_arrays[1];
    quarry_brf_encoder_workspace_t encoder = {
        root_fields,
        13U,
        0U,
        0U,
        encoded_elements,
        16U,
        0U,
        {plans, 4U, frames, 4U, planned_fields, 32U, record_arrays, 1U, 0U, 0U, 0U, 0U}};
    quarry_brf_record_provider_t child = {child_field, NULL};
    root_context_t context = {&child};
    quarry_brf_value_provider_t root = {root_field, &context};
    quarry_brf_record_view_t view;
    quarry_string_view_t text;
    uint64_t value;

    if (argc != 2 || load(argv[1], &qbs, &qbs_size) != 0 ||
        quarry_qbs_parse(qbs, qbs_size, &schema, &parse_workspace, &limits) != QUARRY_GENERIC_OK ||
        quarry_qbs_find_record_by_id(&schema, 1U, &parent) != QUARRY_GENERIC_OK)
        return 1;
    output = (uint8_t*)malloc(1024U);
    quarry_generic_status_t encode_status =
        output == NULL ? QUARRY_GENERIC_INVALID_ARGUMENT
                       : quarry_brf_encode(&schema, parent, &root, output, 1024U, &output_size,
                                           &encoder, NULL);
    if (encode_status != QUARRY_GENERIC_OK) {
        fprintf(stderr, "encode status=%d size=%zu\n", (int)encode_status, output_size);
        return 1;
    }
    quarry_generic_status_t validate_status = quarry_brf_validate_with_workspace(
        &schema, parent, output, output_size, &view, &parse_workspace, &limits);
    if (validate_status != QUARRY_GENERIC_OK) {
        fprintf(stderr, "validate status=%d size=%zu\n", (int)validate_status, output_size);
        return 1;
    }
    quarry_brf_record_view_t child_view;
    if (quarry_brf_get_record(&view, 9U, &child_view) != QUARRY_GENERIC_OK ||
        quarry_brf_get_uint(&child_view, 0U, &value) != QUARRY_GENERIC_OK || value != 100U ||
        quarry_brf_get_string(&child_view, 1U, &text) != QUARRY_GENERIC_OK || text.size != 5U)
        return 1;
    child_failure = 1;
    for (size_t i = 0U; i < 1024U; ++i)
        output[i] = 0xa5U;
    if (quarry_brf_encode(&schema, parent, &root, output, 1024U, &output_size, &encoder, NULL) !=
            QUARRY_GENERIC_RESOURCE_LIMIT ||
        output[0] != 0xa5U || output[1023] != 0xa5U)
        return 1;
    free(output);
    free(qbs);
    puts("nested record encoding: ok");
    return 0;
}
