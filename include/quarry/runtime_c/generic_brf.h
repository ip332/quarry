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
} quarry_generic_limits_t;

typedef struct {
    const quarry_qbs_view_t* qbs;
    const quarry_qbs_record_view_t* schema;
    const uint8_t* bytes;
    size_t size;
    size_t fixed_end;
    size_t tail;
} quarry_brf_record_view_t;

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

#ifdef __cplusplus
}
#endif
#endif
