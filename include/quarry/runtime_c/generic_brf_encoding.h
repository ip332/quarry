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
typedef struct {
    uint32_t parent_record;
    uint16_t field_index;
    quarry_brf_value_t value;
    uint32_t child_record;
    uint32_t array_plan;
    size_t payload_offset;
    size_t payload_size;
    size_t array_start;
    size_t array_count;
    uint8_t present;
} quarry_brf_nested_field_plan_t;
/* A record provider is an instance handle.  QBS supplies the record and field
 * schema; this object supplies only values for the duration of an encode or
 * planning operation. */
typedef struct quarry_brf_record_provider {
    quarry_generic_status_t (*get_field)(const struct quarry_brf_record_provider*, uint16_t,
                                         quarry_brf_value_t*);
    const void* context;
} quarry_brf_record_provider_t;
typedef struct quarry_brf_array_provider {
    quarry_generic_status_t (*get_element)(const struct quarry_brf_array_provider*, size_t,
                                           quarry_brf_value_t*);
    size_t count;
    const void* context;
} quarry_brf_array_provider_t;
/* Record arrays use a distinct callback so aggregate values are never
 * ambiguous or accidentally treated as scalar-element values. */
typedef struct quarry_brf_record_array_provider {
    quarry_generic_status_t (*get_record)(const struct quarry_brf_record_array_provider*, size_t,
                                          const quarry_brf_record_provider_t**);
    size_t count;
    const void* context;
} quarry_brf_record_array_provider_t;
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
    size_t array_start;
    size_t array_count;
} quarry_brf_encoder_field_t;
typedef struct {
    quarry_brf_value_t value;
    size_t payload_offset;
    size_t payload_size;
} quarry_brf_encoder_array_element_t;
typedef struct {
    const quarry_qbs_record_view_t* schema;
    const quarry_brf_record_provider_t* provider;
    uint32_t parent_record;
    uint16_t parent_field;
    uint32_t first_field;
    uint16_t field_count;
    size_t planned_size;
    uint8_t complete;
} quarry_brf_nested_record_plan_t;
typedef struct {
    uint32_t record_plan;
    uint16_t field_cursor;
    size_t destination_offset;
    uint8_t phase;
} quarry_brf_nested_frame_t;
typedef struct {
    uint32_t record_plan;
    uint16_t field_cursor;
    size_t destination_offset;
    uint8_t phase;
} quarry_brf_writer_frame_t;
typedef struct {
    quarry_brf_writer_frame_t* frames;
    size_t frame_capacity;
} quarry_brf_writer_workspace_t;
typedef struct {
    uint32_t parent_record;
    uint16_t parent_field;
    uint32_t first_record;
    uint32_t count;
} quarry_brf_nested_record_array_plan_t;
typedef struct {
    quarry_brf_nested_record_plan_t* records;
    size_t record_capacity;
    quarry_brf_nested_frame_t* frames;
    size_t frame_capacity;
    quarry_brf_nested_field_plan_t* fields;
    size_t field_capacity;
    quarry_brf_nested_record_array_plan_t* arrays;
    size_t array_capacity;
    size_t record_count;
    size_t frame_count;
    size_t field_count;
    size_t array_count;
} quarry_brf_nested_planning_workspace_t;
typedef struct {
    quarry_brf_encoder_field_t* fields;
    size_t field_capacity;
    size_t field_count;
    size_t work_count;
    quarry_brf_encoder_array_element_t* array_elements;
    size_t array_element_capacity;
    size_t array_element_count;
    /* E3 planning stores. Existing positional initializers remain valid. */
    quarry_brf_nested_planning_workspace_t nested;
} quarry_brf_encoder_workspace_t;
void quarry_brf_encoder_workspace_reset(quarry_brf_encoder_workspace_t*);
void quarry_brf_nested_planning_workspace_reset(quarry_brf_nested_planning_workspace_t*);
quarry_generic_status_t quarry_brf_nested_plan_push_record(quarry_brf_nested_planning_workspace_t*,
                                                           const quarry_qbs_record_view_t*,
                                                           const quarry_brf_record_provider_t*,
                                                           uint32_t, uint16_t, uint32_t*);
quarry_generic_status_t quarry_brf_nested_plan_push_frame(quarry_brf_nested_planning_workspace_t*,
                                                          uint32_t, uint32_t*);
quarry_generic_status_t quarry_brf_nested_plan_add_field(quarry_brf_nested_planning_workspace_t*,
                                                         uint32_t, uint16_t,
                                                         const quarry_brf_value_t*, uint32_t*);
quarry_generic_status_t quarry_brf_nested_plan_add_array(quarry_brf_nested_planning_workspace_t*,
                                                         uint32_t, uint16_t, uint32_t, uint32_t*);
quarry_generic_status_t quarry_brf_encode(const quarry_qbs_view_t*, const quarry_qbs_record_view_t*,
                                          const quarry_brf_value_provider_t*, uint8_t*, size_t,
                                          size_t*, quarry_brf_encoder_workspace_t*,
                                          quarry_brf_writer_workspace_t*,
                                          const quarry_brf_encode_limits_t*);
#ifdef __cplusplus
}
#endif
#endif
