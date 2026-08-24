#include "quarry/runtime_c/generic_brf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char* path, uint8_t** out, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
        return 1;
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0)
        return 1;
    *size = (size_t)length;
    *out = (uint8_t*)malloc(*size);
    if (*out == NULL || fread(*out, 1U, *size, file) != *size)
        return 1;
    (void)fclose(file);
    return 0;
}

static int expect(quarry_generic_status_t actual, quarry_generic_status_t wanted) {
    if (actual != wanted)
        (void)fprintf(stderr, "status %d expected %d\n", (int)actual, (int)wanted);
    return actual == wanted ? 0 : 1;
}

int main(void) {
    const char* directory = QUARRY_GENERIC_RUNTIME_FIXTURE_DIR;
    char qbs_path[512];
    char brf_path[512];
    (void)snprintf(qbs_path, sizeof(qbs_path), "%s/schema.qbs", directory);
    (void)snprintf(brf_path, sizeof(brf_path), "%s/record.brf", directory);
    uint8_t* qbs = NULL;
    uint8_t* brf = NULL;
    size_t qbs_size = 0U;
    size_t brf_size = 0U;
    int qread = read_file(qbs_path, &qbs, &qbs_size);
    int bread = read_file(brf_path, &brf, &brf_size);
    if (qread != 0 || bread != 0)
        return 1;
    quarry_qbs_record_view_t records[4];
    quarry_qbs_field_view_t fields[32];
    quarry_qbs_type_view_t types[32];
    quarry_qbs_enum_view_t enums[4];
    uint64_t enum_values[8];
    quarry_workspace_t workspace = {records, 4U,    fields, 32U,         types,
                                    32U,     enums, 4U,     enum_values, 8U};
    quarry_qbs_view_t schema;
    quarry_generic_limits_t limits = {1024U * 1024U, 1024U * 1024U, 1024U};
    if (expect(quarry_qbs_parse(qbs, qbs_size, &schema, &workspace, &limits), QUARRY_GENERIC_OK) !=
        0) {
        (void)fprintf(stderr, "qbs parse failed\n");
        return 1;
    }
    const quarry_qbs_record_view_t* parent = NULL;
    if (expect(quarry_qbs_find_record_by_id(&schema, 1U, &parent), QUARRY_GENERIC_OK) != 0) {
        fprintf(stderr, "record lookup failed\n");
        return 1;
    }
    if (parent->field_count != 13U)
        return 1;
    uint8_t bad_qbs[1172];
    (void)memcpy(bad_qbs, qbs, qbs_size);
    bad_qbs[4] = 2U;
    if (quarry_qbs_parse(bad_qbs, qbs_size, &schema, &workspace, &limits) !=
        QUARRY_GENERIC_MALFORMED_QBS) {
        fprintf(stderr, "bad version\n");
        return 1;
    }
    if (quarry_qbs_parse(qbs, 32U, &schema, &workspace, &limits) != QUARRY_GENERIC_MALFORMED_QBS) {
        fprintf(stderr, "trunc\n");
        return 1;
    }
    (void)memcpy(bad_qbs, qbs, qbs_size);
    bad_qbs[28] = 0xffU;
    if (quarry_qbs_parse(bad_qbs, qbs_size, &schema, &workspace, &limits) !=
        QUARRY_GENERIC_MALFORMED_QBS)
        return 1;
    quarry_workspace_t small_workspace = {records, 0U,    fields, 0U,          types,
                                          0U,      enums, 0U,     enum_values, 0U};
    if (quarry_qbs_parse(qbs, qbs_size, &schema, &small_workspace, &limits) !=
        QUARRY_GENERIC_WORKSPACE_EXHAUSTED)
        return 1;

    /* Phase 1 deliberately excludes present arrays and records.  Clear those
     * fields in a private test copy to exercise the supported scalar path
     * without changing the shared canonical artifact. */
    uint8_t* scalar_brf = (uint8_t*)malloc(brf_size);
    if (scalar_brf == NULL) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }
    (void)memcpy(scalar_brf, brf, brf_size);
    scalar_brf[17] = 0U;
    (void)memset(scalar_brf + 56U, 0, 8U);
    (void)memset(scalar_brf + 64U, 0, 8U);
    (void)memset(scalar_brf + 72U, 0, 8U);
    (void)memset(scalar_brf + 88U, 0, 8U);
    scalar_brf[12] = 0U;
    scalar_brf[13] = 0U;
    scalar_brf[14] = 0U;
    scalar_brf[15] = 105U;
    quarry_brf_record_view_t record;
    if (expect(quarry_brf_validate(&schema, parent, scalar_brf, 105U, &record, &limits),
               QUARRY_GENERIC_OK) != 0) {
        fprintf(stderr, "brf validate failed\n");
        return 1;
    }
    uint64_t u;
    int64_t i;
    bool b;
    float f;
    double d;
    int64_t e;
    quarry_string_view_t text;
    quarry_bytes_view_t bytes;
    bool present;
    if (expect(quarry_brf_get_uint(&record, 0U, &u), QUARRY_GENERIC_OK) != 0 || u != 42U) {
        fprintf(stderr, "uint %llu\n", (unsigned long long)u);
        return 1;
    }
    if (expect(quarry_brf_get_int(&record, 1U, &i), QUARRY_GENERIC_OK) != 0 || i != -17) {
        fprintf(stderr, "int %lld\n", (long long)i);
        return 1;
    }
    if (expect(quarry_brf_get_bool(&record, 2U, &b), QUARRY_GENERIC_OK) != 0 || !b) {
        fprintf(stderr, "bool %d\n", (int)b);
        return 1;
    }
    if (expect(quarry_brf_get_float(&record, 3U, &f), QUARRY_GENERIC_OK) != 0 || f != 12.5F) {
        fprintf(stderr, "float %f\n", (double)f);
        return 1;
    }
    if (expect(quarry_brf_get_double(&record, 4U, &d), QUARRY_GENERIC_OK) != 0 || d != -3.25) {
        fprintf(stderr, "double %f\n", d);
        return 1;
    }
    if (expect(quarry_brf_get_enum(&record, 5U, &e), QUARRY_GENERIC_OK) != 0 || e != 1) {
        fprintf(stderr, "enum %lld\n", (long long)e);
        return 1;
    }
    if (expect(quarry_brf_get_string(&record, 6U, &text), QUARRY_GENERIC_OK) != 0 ||
        text.size != 6U || memcmp(text.data, "quarry", 6U) != 0) {
        fprintf(stderr, "string %zu\n", text.size);
        return 1;
    }
    if (expect(quarry_brf_get_bytes(&record, 7U, &bytes), QUARRY_GENERIC_OK) != 0 ||
        bytes.size != 3U || bytes.data[2] != 0xffU)
        return 1;
    if (expect(quarry_brf_field_is_present(&record, 11U, &present), QUARRY_GENERIC_OK) != 0 ||
        present)
        return 1;
    if (expect(quarry_brf_get_uint(&record, 6U, &u), QUARRY_GENERIC_TYPE_MISMATCH) != 0)
        return 1;
    scalar_brf[26] = 2U;
    if (quarry_brf_validate(&schema, parent, scalar_brf, 105U, &record, &limits) !=
        QUARRY_GENERIC_MALFORMED_BRF)
        return 1;
    scalar_brf[26] = 1U;
    scalar_brf[17] = 0x80U;
    if (quarry_brf_validate(&schema, parent, scalar_brf, 105U, &record, &limits) !=
        QUARRY_GENERIC_MALFORMED_BRF)
        return 1;
    if (quarry_brf_validate(&schema, parent, brf, brf_size, &record, &limits) !=
        QUARRY_GENERIC_UNSUPPORTED_TYPE) {
        fprintf(stderr, "unsupported\n");
        return 1;
    }
    free(scalar_brf);
    free(brf);
    free(qbs);
    return 0;
}
