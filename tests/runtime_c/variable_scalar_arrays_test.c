#include "quarry/runtime_c/generic_brf.h"
#include "quarry/runtime_c/generic_brf_encoding.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* strings[4];
    size_t string_lengths[4];
    const uint8_t* blobs[4];
    size_t blob_lengths[4];
    size_t count;
    size_t lookups;
} array_context_t;

static quarry_generic_status_t array_element(const quarry_brf_array_provider_t* p, size_t index,
                                              quarry_brf_value_t* out) {
    array_context_t* c = (array_context_t*)p->context;
    ++c->lookups;
    if (index >= c->count) return QUARRY_GENERIC_FIELD_NOT_FOUND;
    if (p->count == c->count && c->strings[0] != NULL) {
        *out = (quarry_brf_value_t){.kind = QUARRY_BRF_ENCODE_STRING,
                                    .string_value = {c->strings[index], c->string_lengths[index]}};
    } else {
        *out = (quarry_brf_value_t){.kind = QUARRY_BRF_ENCODE_BYTES,
                                    .bytes_value = {c->blobs[index], c->blob_lengths[index]}};
    }
    return QUARRY_GENERIC_OK;
}

typedef struct {
    array_context_t* strings;
    array_context_t* blobs;
    int present_strings;
    int present_blobs;
} root_context_t;

static quarry_generic_status_t root_field(const quarry_brf_value_provider_t* p, uint16_t index,
                                          quarry_brf_value_t* out) {
    root_context_t* c = (root_context_t*)p->context;
    *out = (quarry_brf_value_t){0};
    if (index == 0U && c->present_strings) {
        static quarry_brf_array_provider_t provider;
        provider = (quarry_brf_array_provider_t){array_element, c->strings->count, c->strings};
        out->kind = QUARRY_BRF_ENCODE_ARRAY; out->aggregate = &provider;
    } else if (index == 1U && c->present_blobs) {
        static quarry_brf_array_provider_t provider;
        provider = (quarry_brf_array_provider_t){array_element, c->blobs->count, c->blobs};
        out->kind = QUARRY_BRF_ENCODE_ARRAY; out->aggregate = &provider;
    }
    return QUARRY_GENERIC_OK;
}

static int load_file(const char* path, uint8_t** data, size_t* size) {
    FILE* f = fopen(path, "rb"); long n;
    if (f == NULL || fseek(f, 0L, SEEK_END) != 0) return 1;
    n = ftell(f); if (n < 0L || fseek(f, 0L, SEEK_SET) != 0) { fclose(f); return 1; }
    *size = (size_t)n; *data = (uint8_t*)malloc(*size);
    if (*data == NULL || fread(*data, 1U, *size, f) != *size) { free(*data); *data = NULL; fclose(f); return 1; }
    fclose(f); return 0;
}

int main(int argc, char** argv) {
    uint8_t* qbs; size_t qbs_size; quarry_qbs_view_t q = {0};
    quarry_qbs_record_view_t records[4]; quarry_qbs_field_view_t fields[8];
    quarry_qbs_type_view_t types[8]; quarry_qbs_enum_view_t enums[1]; uint64_t values[1];
    quarry_brf_record_node_t nodes[1]; quarry_brf_field_state_t states[1]; uint32_t maps[1];
    quarry_brf_child_relation_t children[1]; quarry_brf_record_array_relation_t arrays[1];
    uint32_t array_elements[1]; quarry_brf_validation_frame_t frames[1];
    quarry_workspace_t ws = {records, 4U, fields, 8U, types, 8U, enums, 1U, values, 1U,
                             nodes, 1U, states, 1U, maps, 1U, children, 1U, arrays, 1U,
                             array_elements, 1U, frames, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    quarry_generic_limits_t limits = {1U << 20U, 1U << 20U, 1024U, 16U, 16U};
    const quarry_qbs_record_view_t* root; uint8_t output[512]; size_t size;
    if (argc != 2 || load_file(argv[1], &qbs, &qbs_size) != 0) { fprintf(stderr, "load\n"); return 1; }
    if (quarry_qbs_parse(qbs, qbs_size, &q, &ws, &limits) != QUARRY_GENERIC_OK) { fprintf(stderr, "parse\n"); return 1; }
    if (quarry_qbs_find_record_by_id(&q, 1U, &root) != QUARRY_GENERIC_OK || root->field_count != 2U) { fprintf(stderr, "root %u\n", root == NULL ? 0U : root->field_count); return 1; }
    assert(q.types[q.fields[root->field_start].type_index].code == 16U);
    assert(q.types[q.fields[root->field_start + 1U].type_index].code == 16U);
    assert(q.types[q.types[q.fields[root->field_start].type_index].reference].code == 13U);
    assert(q.types[q.types[q.fields[root->field_start + 1U].type_index].reference].code == 14U);
    static quarry_brf_encoder_field_t planned_fields[2];
    static quarry_brf_encoder_array_element_t planned_elements[8];
    quarry_brf_encoder_workspace_t encoder = {planned_fields, 2U, 0U, 0U, planned_elements, 8U, 0U, {0}};
    const char* names[4] = {"A", "hello", "é", "xyz"};
    const uint8_t b0[] = {0x00U, 0xffU}; const uint8_t b1[] = {0x10U}; const uint8_t b2[] = {0U};
    array_context_t strings = {{names[0], names[1], names[2], names[3]}, {1U, 5U, 2U, 3U}, {0}, {0}, 3U, 0U};
    array_context_t blobs = {{0}, {0}, {b0, b1, b2, NULL}, {2U, 1U, 0U, 0U}, 3U, 0U};
    root_context_t context = {&strings, &blobs, 1, 0};
    quarry_brf_value_provider_t provider = {root_field, &context};
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(strings.lookups == 3U);
    static const uint8_t expected_strings[] = {
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x2d,
        0x01,0x00,0x00,0x00,0x21,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x03,0x01,0x41,0x05,0x68,0x65,0x6c,0x6c,0x6f,0x02,0xc3,0xa9};
    assert(size == sizeof(expected_strings) && memcmp(output, expected_strings, size) == 0);
    assert(size > 0U);

    /* Present-empty arrays retain presence but require no element lookup. */
    strings.count = 0U; blobs.count = 0U;
    strings.lookups = blobs.lookups = 0U;
    context.present_strings = 1; context.present_blobs = 1;
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(strings.lookups == 0U && blobs.lookups == 0U);
    assert(size > 0U);
    /* Both array fields are present, and each payload is the canonical zero count. */
    assert(output[size - 2U] == 0U && output[size - 1U] == 0U);

    /* Zero-length elements use a length prefix with no payload bytes. */
    names[0] = ""; strings.strings[0] = names[0]; strings.string_lengths[0] = 0U;
    strings.count = 3U; blobs.count = 3U;
    blobs.blobs[0] = b0; blobs.blob_lengths[0] = 0U;
    blobs.blobs[1] = b1; blobs.blob_lengths[1] = 0U;
    blobs.blobs[2] = b0; blobs.blob_lengths[2] = 2U;
    strings.lookups = blobs.lookups = 0U;
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(strings.lookups == 3U && blobs.lookups == 3U);
    assert(size > 0U);

    /* Absent arrays produce no array payload and do not enumerate elements. */
    context.present_strings = 0; context.present_blobs = 0;
    strings.lookups = blobs.lookups = 0U;
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(strings.lookups == 0U && blobs.lookups == 0U);
    assert(size > 0U);

    names[0] = "A"; names[1] = "hello"; names[2] = "é";
    strings.strings[0] = names[0]; strings.string_lengths[0] = 1U;
    strings.string_lengths[1] = 5U; strings.string_lengths[2] = 2U;
    blobs.blobs[0] = b0; blobs.blob_lengths[0] = 2U;
    blobs.blobs[1] = b1; blobs.blob_lengths[1] = 1U;
    blobs.blobs[2] = b2; blobs.blob_lengths[2] = 0U;
    context.present_strings = 0; context.present_blobs = 1; strings.lookups = blobs.lookups = 0U;
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(blobs.lookups == 3U);
    static const uint8_t expected_blobs[] = {
        0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x28,
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x21,0x00,0x00,0x00,
        0x07,0x03,0x02,0x00,0xff,0x01,0x10,0x00};
    assert(size == sizeof(expected_blobs) && memcmp(output, expected_blobs, size) == 0);
    context.present_strings = 1; context.present_blobs = 1;
    strings.count = blobs.count = 4U; strings.lookups = blobs.lookups = 0U;
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_OK);
    assert(strings.lookups == 4U && blobs.lookups == 4U);
    {
        const size_t required = size;
        memset(output, 0x5a, sizeof(output));
        assert(quarry_brf_encode(&q, root, &provider, output, required - 1U, &size, &encoder, NULL, NULL) ==
               QUARRY_GENERIC_BUFFER_TOO_SMALL);
        for (size_t i = 0U; i < required - 1U; ++i) assert(output[i] == 0x5aU);
    }
    context.present_blobs = 0;
    strings.count = 3U;
    encoder.array_element_capacity = 2U;
    memset(output, 0x5a, sizeof(output));
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) ==
           QUARRY_GENERIC_WORKSPACE_EXHAUSTED);
    assert(output[0] == 0x5aU && output[511] == 0x5aU);
    encoder.array_element_capacity = 8U;
    context.present_blobs = 1;
    strings.count = 5U;
    memset(output, 0xa5, sizeof(output));
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_VALUE_OUT_OF_RANGE);
    assert(output[0] == 0xa5U && output[511] == 0xa5U);
    strings.count = 3U;
    names[2] = "\xff"; strings.strings[2] = names[2]; strings.string_lengths[2] = 1U;
    memset(output, 0x5a, sizeof(output));
    assert(quarry_brf_encode(&q, root, &provider, output, sizeof(output), &size, &encoder, NULL, NULL) == QUARRY_GENERIC_INVALID_ARGUMENT);
    assert(output[0] == 0x5aU && output[511] == 0x5aU);
    free(qbs); puts("variable scalar arrays: ok"); return 0;
}
