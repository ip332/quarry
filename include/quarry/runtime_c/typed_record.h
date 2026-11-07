#pragma once

/* Generated host-structure bindings for the shared BRF v2 codec.
 * Descriptors are immutable compiler output, not untrusted input. */
#include <quarry/runtime_c/binary_record_v2.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUARRY_C_TYPED_BOOL,
    QUARRY_C_TYPED_I8,
    QUARRY_C_TYPED_U8,
    QUARRY_C_TYPED_I16,
    QUARRY_C_TYPED_U16,
    QUARRY_C_TYPED_I32,
    QUARRY_C_TYPED_U32,
    QUARRY_C_TYPED_I64,
    QUARRY_C_TYPED_U64,
    QUARRY_C_TYPED_F32,
    QUARRY_C_TYPED_F64,
    QUARRY_C_TYPED_ENUM,
    QUARRY_C_TYPED_STRING,
    QUARRY_C_TYPED_BYTES,
    QUARRY_C_TYPED_RECORD
} quarry_c_typed_kind_t;

struct quarry_c_type_descriptor;
typedef struct {
    uint32_t kind; /* quarry_c_typed_kind_t; fixed width across enum compiler options. */
    size_t size;
    size_t length_offset; /* Relative to the element for string/bytes. */
    uint32_t bound;
    uint8_t wire_width;
    const struct quarry_c_type_descriptor* record;
    uint32_t record_id; /* Resolved ID in the containing schema. */
    bool fixed_record;
    const uint64_t* enum_values;
    size_t enum_count;
    uint64_t (*enum_load)(const void*);
    void (*enum_store)(void*, uint64_t);
} quarry_c_element_descriptor_t;

typedef struct {
    size_t presence_offset;
    size_t value_offset;
    size_t count_offset;
    uint32_t max_count; /* Zero for a non-array field. */
    quarry_c_element_descriptor_t element;
} quarry_c_member_descriptor_t;

typedef struct quarry_c_type_descriptor {
    const quarry_c_brf_v2_record_layout_t* layout;
    size_t size;
    const quarry_c_member_descriptor_t* members;
} quarry_c_type_descriptor_t;

typedef struct {
    uint32_t status; /* quarry_c_status_t; fixed-width shared-runtime ABI. */
    size_t bytes_written;
} quarry_c_typed_encode_result_t;

typedef struct {
    uint32_t status; /* quarry_c_status_t; fixed-width shared-runtime ABI. */
    bool has_byte_offset;
    size_t byte_offset;
} quarry_c_typed_decode_result_t;

typedef struct {
    uint32_t status; /* quarry_c_status_t; fixed-width shared-runtime ABI. */
    size_t size;     /* Encoded byte count on success; zero on failure. */
} quarry_c_typed_encoded_size_result_t;

/* Reports the same validation statuses as encode. The source value must remain
 * stable during sizing/encoding. No hidden allocation is performed. */
quarry_c_typed_encoded_size_result_t quarry_c_typed_encoded_size(
    const quarry_c_type_descriptor_t* type, const void* value);
/* The record ID is explicit so imported records can use the containing
 * schema's resolved ID. Invalid values/capacity leave output unchanged.
 * Output must not overlap the source struct. */
quarry_c_typed_encode_result_t quarry_c_typed_encode(const quarry_c_type_descriptor_t* type,
                                                     const void* value, uint32_t record_id,
                                                     uint8_t* output, size_t capacity);
/* Initializes the destination to zero. On failure it may contain partially
 * decoded fields; check status before using it. Input must not overlap value.
 * Error offsets are relative to the root input, including nested failures. */
quarry_c_typed_decode_result_t quarry_c_typed_decode(const quarry_c_type_descriptor_t* type,
                                                     const uint8_t* input, size_t length,
                                                     uint32_t record_id, void* value);

#ifdef __cplusplus
}
#endif
