#ifndef QUARRY_RUNTIME_C_GENERIC_BRF_H_
#define QUARRY_RUNTIME_C_GENERIC_BRF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUARRY_GENERIC_OK = 0,
    QUARRY_GENERIC_INVALID_ARGUMENT,
    QUARRY_GENERIC_MALFORMED_QBS,
    QUARRY_GENERIC_MALFORMED_BRF,
    QUARRY_GENERIC_TYPE_MISMATCH,
    QUARRY_GENERIC_FIELD_ABSENT,
    QUARRY_GENERIC_FIELD_NOT_FOUND,
    QUARRY_GENERIC_RESOURCE_LIMIT,
    QUARRY_GENERIC_WORKSPACE_EXHAUSTED,
    QUARRY_GENERIC_UNSUPPORTED_TYPE
} quarry_generic_status_t;

typedef struct {
    const char* data;
    size_t size;
} quarry_string_view_t;
typedef struct {
    const uint8_t* data;
    size_t size;
} quarry_bytes_view_t;

typedef struct {
    uint32_t record_id;
    uint32_t field_start;
    uint16_t field_count;
    uint8_t variable_size;
    uint32_t presence_bitmap_size;
    uint32_t fixed_region_size;
    uint32_t complete_fixed_record_size;
    uint32_t identity_offset;
    uint16_t name_index;
} quarry_qbs_record_view_t;

typedef struct {
    uint16_t field_index;
    uint16_t type_index;
    uint32_t byte_offset;
    uint32_t bit_width;
    uint16_t presence_bit;
    uint32_t slot_size;
    uint8_t storage;
    uint8_t descriptor_kind;
    uint16_t name_index;
} quarry_qbs_field_view_t;

typedef struct {
    uint8_t code;
    uint8_t fixed;
    uint16_t encoded_width;
    uint16_t reference;
    uint32_t max_elements;
    uint32_t max_bytes;
} quarry_qbs_type_view_t;

typedef struct {
    uint16_t encoded_width;
    uint32_t value_start;
    uint32_t value_count;
    uint32_t identity_offset;
    uint16_t name_index;
} quarry_qbs_enum_view_t;

typedef struct {
    uint16_t element_type;
    uint8_t element_code;
    uint32_t count;
    size_t payload_offset;
    size_t payload_size;
    uint32_t relation_index;
} quarry_brf_array_view_t;

typedef struct quarry_brf_record_view quarry_brf_record_view_t;

typedef struct {
    uint32_t node_index;
    uint32_t qbs_record_index;
    size_t brf_offset;
    size_t brf_size;
    uint16_t field_count;
    uint32_t field_map_start;
    uint8_t complete;
} quarry_brf_record_node_t;

typedef struct {
    uint16_t field_index;
    uint8_t present;
    size_t fixed_offset;
    size_t fixed_size;
    size_t payload_offset;
    size_t payload_size;
    uint32_t child_relation;
    uint32_t array_relation;
} quarry_brf_field_state_t;

typedef struct {
    uint32_t parent_node;
    uint16_t parent_field;
    uint32_t child_node;
    size_t brf_offset;
    size_t brf_size;
} quarry_brf_child_relation_t;

typedef struct {
    uint32_t parent_node;
    uint16_t parent_field;
    uint32_t element_start;
    uint32_t count;
} quarry_brf_record_array_relation_t;

typedef struct {
    uint32_t node_index;
    uint32_t qbs_record_index;
    size_t brf_offset;
    size_t brf_size;
    uint16_t field_cursor;
    size_t variable_cursor;
    uint32_t pending_relation;
    uint32_t pending_array;
    uint32_t pending_index;
    size_t pending_offset;
    size_t pending_size;
    size_t array_cursor;
    size_t array_end;
    uint32_t array_count;
    uint16_t array_element_type;
    uint8_t array_variable_elements;
    uint8_t phase;
} quarry_brf_validation_frame_t;

typedef struct {
    quarry_qbs_record_view_t* records;
    size_t record_capacity;
    quarry_qbs_field_view_t* fields;
    size_t field_capacity;
    quarry_qbs_type_view_t* types;
    size_t type_capacity;
    quarry_qbs_enum_view_t* enums;
    size_t enum_capacity;
    uint64_t* enum_values;
    size_t enum_value_capacity;
    quarry_brf_record_node_t* nodes;
    size_t node_capacity;
    quarry_brf_field_state_t* field_states;
    size_t field_state_capacity;
    uint32_t* field_maps;
    size_t field_map_capacity;
    quarry_brf_child_relation_t* children;
    size_t child_capacity;
    quarry_brf_record_array_relation_t* arrays;
    size_t array_capacity;
    uint32_t* array_elements;
    size_t array_element_capacity;
    quarry_brf_validation_frame_t* frames;
    size_t frame_capacity;
    size_t node_count;
    size_t field_state_count;
    size_t field_map_count;
    size_t child_count;
    size_t array_count;
    size_t array_element_count;
    size_t frame_high_water;
} quarry_workspace_t;

/* Clears caller-owned workspace entries while preserving capacities. */
void quarry_workspace_reset(quarry_workspace_t* workspace);

typedef struct {
    const uint8_t* bytes;
    size_t size;
    quarry_qbs_record_view_t* records;
    size_t record_count;
    quarry_qbs_field_view_t* fields;
    size_t field_count;
    quarry_qbs_type_view_t* types;
    size_t type_count;
    quarry_qbs_enum_view_t* enums;
    size_t enum_count;
    uint64_t* enum_values;
    size_t enum_value_count;
    uint32_t iss_offset;
    uint32_t iss_size;
    uint32_t strings_offset;
    uint32_t strings_size;
    uint8_t identity_width;
} quarry_qbs_view_t;

typedef struct {
    size_t max_image_size;
    size_t max_record_bytes;
    size_t max_work_items;
    size_t max_nested_records;
    size_t max_array_elements;
} quarry_generic_limits_t;

struct quarry_brf_record_view {
    const quarry_qbs_view_t* qbs;
    const quarry_qbs_record_view_t* schema;
    const uint8_t* bytes;
    const uint8_t* root_bytes;
    size_t size;
    size_t fixed_end;
    size_t tail;
    quarry_workspace_t* workspace;
    uint32_t node_index;
};

quarry_generic_status_t quarry_qbs_parse(const uint8_t*, size_t, quarry_qbs_view_t*,
                                         quarry_workspace_t*, const quarry_generic_limits_t*);
quarry_generic_status_t quarry_qbs_find_record_by_id(const quarry_qbs_view_t*, uint32_t,
                                                     const quarry_qbs_record_view_t**);
quarry_generic_status_t quarry_qbs_find_record_by_name(const quarry_qbs_view_t*, const char*,
                                                       size_t, const quarry_qbs_record_view_t**);
quarry_generic_status_t quarry_qbs_record_field(const quarry_qbs_view_t*,
                                                const quarry_qbs_record_view_t*, uint16_t,
                                                const quarry_qbs_field_view_t**);
quarry_generic_status_t quarry_brf_validate(const quarry_qbs_view_t*,
                                            const quarry_qbs_record_view_t*, const uint8_t*, size_t,
                                            quarry_brf_record_view_t*,
                                            const quarry_generic_limits_t*);
quarry_generic_status_t quarry_brf_validate_with_workspace(
    const quarry_qbs_view_t*, const quarry_qbs_record_view_t*, const uint8_t*, size_t,
    quarry_brf_record_view_t*, quarry_workspace_t*, const quarry_generic_limits_t*);
quarry_generic_status_t quarry_brf_field_is_present(const quarry_brf_record_view_t*, uint16_t,
                                                    bool*);
quarry_generic_status_t quarry_brf_get_uint(const quarry_brf_record_view_t*, uint16_t, uint64_t*);
quarry_generic_status_t quarry_brf_get_int(const quarry_brf_record_view_t*, uint16_t, int64_t*);
quarry_generic_status_t quarry_brf_get_bool(const quarry_brf_record_view_t*, uint16_t, bool*);
quarry_generic_status_t quarry_brf_get_float(const quarry_brf_record_view_t*, uint16_t, float*);
quarry_generic_status_t quarry_brf_get_double(const quarry_brf_record_view_t*, uint16_t, double*);
quarry_generic_status_t quarry_brf_get_enum(const quarry_brf_record_view_t*, uint16_t, int64_t*);
quarry_generic_status_t quarry_brf_get_string(const quarry_brf_record_view_t*, uint16_t,
                                              quarry_string_view_t*);
quarry_generic_status_t quarry_brf_get_bytes(const quarry_brf_record_view_t*, uint16_t,
                                             quarry_bytes_view_t*);
quarry_generic_status_t quarry_brf_get_array(const quarry_brf_record_view_t*, uint16_t,
                                             quarry_brf_array_view_t*);
quarry_generic_status_t quarry_brf_array_get_uint(const quarry_brf_record_view_t*,
                                                  const quarry_brf_array_view_t*, size_t, uint64_t*);
quarry_generic_status_t quarry_brf_array_get_int(const quarry_brf_record_view_t*,
                                                 const quarry_brf_array_view_t*, size_t, int64_t*);
quarry_generic_status_t quarry_brf_array_get_bool(const quarry_brf_record_view_t*,
                                                  const quarry_brf_array_view_t*, size_t, bool*);
quarry_generic_status_t quarry_brf_array_get_float(const quarry_brf_record_view_t*,
                                                   const quarry_brf_array_view_t*, size_t, float*);
quarry_generic_status_t quarry_brf_array_get_double(const quarry_brf_record_view_t*,
                                                    const quarry_brf_array_view_t*, size_t, double*);
quarry_generic_status_t quarry_brf_array_get_enum(const quarry_brf_record_view_t*,
                                                  const quarry_brf_array_view_t*, size_t, int64_t*);
quarry_generic_status_t quarry_brf_get_record(const quarry_brf_record_view_t*, uint16_t,
                                              quarry_brf_record_view_t*);
quarry_generic_status_t quarry_brf_get_record_array(const quarry_brf_record_view_t*, uint16_t,
                                                    quarry_brf_array_view_t*);
quarry_generic_status_t quarry_brf_record_array_get(const quarry_brf_record_view_t*,
                                                    const quarry_brf_array_view_t*, size_t,
                                                    quarry_brf_record_view_t*);

typedef enum {
    QUARRY_BRF_EVENT_RECORD_BEGIN = 0,
    QUARRY_BRF_EVENT_RECORD_END,
    QUARRY_BRF_EVENT_FIELD,
    QUARRY_BRF_EVENT_SCALAR,
    QUARRY_BRF_EVENT_ARRAY_BEGIN,
    QUARRY_BRF_EVENT_ARRAY_ELEMENT,
    QUARRY_BRF_EVENT_ARRAY_END
} quarry_brf_traversal_event_kind_t;

typedef enum {
    QUARRY_BRF_SCALAR_UINT = 0,
    QUARRY_BRF_SCALAR_INT,
    QUARRY_BRF_SCALAR_BOOL,
    QUARRY_BRF_SCALAR_FLOAT,
    QUARRY_BRF_SCALAR_DOUBLE,
    QUARRY_BRF_SCALAR_ENUM,
    QUARRY_BRF_SCALAR_STRING,
    QUARRY_BRF_SCALAR_BYTES
} quarry_brf_scalar_kind_t;

typedef struct {
    quarry_brf_scalar_kind_t kind;
    uint64_t uint_value;
    int64_t int_value;
    bool bool_value;
    float float_value;
    double double_value;
    quarry_string_view_t string_value;
    quarry_bytes_view_t bytes_value;
} quarry_brf_scalar_t;

typedef struct {
    quarry_brf_traversal_event_kind_t kind;
    uint16_t field_index;
    bool present;
    size_t index;
    size_t depth;
    quarry_brf_scalar_t scalar;
    quarry_brf_array_view_t array;
    quarry_brf_record_view_t record;
} quarry_brf_traversal_event_t;

typedef enum {
    QUARRY_BRF_TRAVERSAL_CONTINUE = 0,
    QUARRY_BRF_TRAVERSAL_STOP
} quarry_brf_traversal_control_t;

typedef enum {
    QUARRY_BRF_TRAVERSAL_COMPLETED = 0,
    QUARRY_BRF_TRAVERSAL_STOPPED,
    QUARRY_BRF_TRAVERSAL_WORK_LIMIT,
    QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT,
    QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED,
    QUARRY_BRF_TRAVERSAL_INVALID_ARGUMENT
} quarry_brf_traversal_result_t;

typedef quarry_brf_traversal_control_t (*quarry_brf_traversal_callback_t)(
    const quarry_brf_traversal_event_t*, void*);

typedef struct {
    quarry_brf_record_view_t record;
    quarry_brf_array_view_t array;
    uint16_t field_index;
    size_t next;
    size_t depth;
    uint8_t kind;
    uint8_t began;
} quarry_brf_traversal_frame_t;

typedef struct {
    quarry_brf_traversal_frame_t* frames;
    size_t frame_capacity;
    size_t frame_high_water;
    size_t work_count;
} quarry_brf_traversal_workspace_t;

typedef struct {
    size_t max_work_items;
    size_t max_depth;
} quarry_brf_traversal_limits_t;

void quarry_brf_traversal_workspace_reset(quarry_brf_traversal_workspace_t*);
quarry_brf_traversal_result_t quarry_brf_traverse(
    const quarry_brf_record_view_t*, quarry_brf_traversal_callback_t, void*,
    quarry_brf_traversal_workspace_t*, const quarry_brf_traversal_limits_t*);

#ifdef __cplusplus
}
#endif
#endif
