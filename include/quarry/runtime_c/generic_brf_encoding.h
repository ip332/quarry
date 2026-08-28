#ifndef QUARRY_RUNTIME_C_GENERIC_BRF_ENCODING_H_
#define QUARRY_RUNTIME_C_GENERIC_BRF_ENCODING_H_
#include "quarry/runtime_c/generic_brf.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    QUARRY_BRF_ENCODE_ABSENT = 0,
    QUARRY_BRF_ENCODE_UINT,
    QUARRY_BRF_ENCODE_INT,
    QUARRY_BRF_ENCODE_BOOL,
    QUARRY_BRF_ENCODE_FLOAT,
    QUARRY_BRF_ENCODE_DOUBLE,
    QUARRY_BRF_ENCODE_ENUM,
    QUARRY_BRF_ENCODE_STRING,
    QUARRY_BRF_ENCODE_BYTES,
    QUARRY_BRF_ENCODE_ARRAY,
    QUARRY_BRF_ENCODE_RECORD
} quarry_brf_encode_kind_t;
typedef struct {
    quarry_brf_encode_kind_t kind;
    uint64_t uint_value;
    int64_t int_value;
    bool bool_value;
    float float_value;
    double double_value;
    quarry_string_view_t string_value;
    quarry_bytes_view_t bytes_value;
    const void* aggregate;
} quarry_brf_value_t;
typedef struct quarry_brf_value_provider {
    quarry_generic_status_t (*get_field)(const struct quarry_brf_value_provider*, uint16_t,
                                         quarry_brf_value_t*);
    const void* context;
} quarry_brf_value_provider_t;
typedef struct {
    size_t max_record_bytes;
    size_t max_work_items;
} quarry_brf_encode_limits_t;
typedef struct {
    uint16_t field_index;
    quarry_brf_value_t value;
    size_t payload_offset;
    size_t payload_size;
    uint8_t present;
} quarry_brf_encoder_field_t;
typedef struct {
    quarry_brf_encoder_field_t* fields;
    size_t field_capacity;
    size_t field_count;
    size_t work_count;
} quarry_brf_encoder_workspace_t;
void quarry_brf_encoder_workspace_reset(quarry_brf_encoder_workspace_t*);
quarry_generic_status_t quarry_brf_encode(const quarry_qbs_view_t*, const quarry_qbs_record_view_t*,
                                          const quarry_brf_value_provider_t*, uint8_t*, size_t,
                                          size_t*, quarry_brf_encoder_workspace_t*,
                                          const quarry_brf_encode_limits_t*);
#ifdef __cplusplus
}
#endif
#endif
