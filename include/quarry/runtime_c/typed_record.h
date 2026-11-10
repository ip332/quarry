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

/* Presence operations are resolved by the generator: bool, packed bit, or
 * nonzero uint32 length/count. No field-kind classification is needed at encode. */
#define QUARRY_C_PRESENCE_PACKED_FLAG ((size_t)1U << (sizeof(size_t) * 8U - 1U))
#define QUARRY_C_PRESENCE_COUNT_FLAG ((size_t)1U << (sizeof(size_t) * 8U - 2U))
#define QUARRY_C_COUNT_PRESENCE(byte_offset) (QUARRY_C_PRESENCE_COUNT_FLAG | (size_t)(byte_offset))
#define QUARRY_C_PACKED_PRESENCE(byte_offset, bit)                                                 \
    (QUARRY_C_PRESENCE_PACKED_FLAG | ((size_t)(byte_offset) * 8U + (bit)))

typedef struct {
    size_t presence_offset; /* Bool offset, QUARRY_C_PACKED_PRESENCE, or QUARRY_C_COUNT_PRESENCE. */
    size_t value_offset;
    size_t count_offset;
    uint32_t max_count; /* Zero for a non-array field. */
    quarry_c_element_descriptor_t element;
} quarry_c_member_descriptor_t;

/* Generated per-field decode operations, one per layout field in layout order.
 * The generator resolves everything decode would otherwise rediscover from the
 * layout and member descriptors on every record: wire presence byte/mask, slot
 * width, scalar copy width, and the native presence/value operation. They are
 * immutable schema metadata (no runtime initialization, cache or state) and must
 * describe exactly the same layout and members as the type descriptor. */
typedef enum {
    QUARRY_C_DECODE_FIXED_1, /* Integer/float bit copy; slot and native width 1, 2, 4 or 8. */
    QUARRY_C_DECODE_FIXED_2,
    QUARRY_C_DECODE_FIXED_4,
    QUARRY_C_DECODE_FIXED_8,
    QUARRY_C_DECODE_BOOL,          /* One-byte slot; only 0 or 1 is valid. */
    QUARRY_C_DECODE_SCALAR,        /* Other fixed scalar (enum): member element decode. */
    QUARRY_C_DECODE_FIXED_FIELD,   /* Fixed or inline-fixed slot decoded by the member. */
    QUARRY_C_DECODE_VARIABLE_FIELD /* Descriptor slot; payload decoded by the member. */
} quarry_c_decode_kind_t;

typedef enum {
    QUARRY_C_DECODE_PRESENCE_BOOL,         /* bool at presence_offset. */
    QUARRY_C_DECODE_PRESENCE_PACKED,       /* native_mask in byte presence_offset. */
    QUARRY_C_DECODE_PRESENCE_PACKED_FIRST, /* As PACKED; first bit of its byte (initializes it). */
    QUARRY_C_DECODE_PRESENCE_COUNT,        /* uint32 length/count at presence_offset. */
    QUARRY_C_DECODE_PRESENCE_COUNT_STRING  /* As COUNT; also NUL-terminates an empty string. */
} quarry_c_decode_presence_t;

typedef struct {
    uint32_t wire_offset;    /* Slot byte offset (layout byte_offset). */
    uint32_t slot_size;      /* Slot size (layout slot_size). */
    uint32_t presence_byte;  /* 16 + presence_bit_index / 8: wire presence byte offset. */
    uint8_t presence_mask;   /* 1 << (presence_bit_index % 8). */
    uint8_t kind;            /* quarry_c_decode_kind_t; fixed width across enum options. */
    uint8_t presence;        /* quarry_c_decode_presence_t. */
    uint8_t native_mask;     /* Packed presence: 1 << native bit; otherwise 0. */
    size_t presence_offset;  /* Native bool, packed byte, or count/length offset (no flags). */
    size_t value_offset;     /* Native value offset. */
    size_t clear_size;       /* Bytes zeroed at value_offset when absent (0 for records/counted). */
} quarry_c_field_decode_op_t;

typedef struct quarry_c_type_descriptor {
    const quarry_c_brf_v2_record_layout_t* layout;
    size_t size;
    const quarry_c_member_descriptor_t* members;
    /* Generated decode operations (layout->field_count entries), or NULL. NULL
     * decodes from the layout and member descriptors alone, with identical
     * results; generated code always supplies them. */
    const quarry_c_field_decode_op_t* decode_ops;
} quarry_c_type_descriptor_t;

typedef struct {
    uint32_t status; /* quarry_c_status_t; fixed-width shared-runtime ABI. */
    size_t bytes_written; /* Output span touched, including on failure; not a valid length on error. */
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
 * schema's resolved ID. Fields are validated as they are written. On failure,
 * bytes_written reports the output span touched, including header/fixed-region
 * clearing and partial nested writes. It never exceeds capacity; bytes beyond
 * it are untouched. On error this span is not a valid record: discard or clear
 * it before reuse. No rollback or automatic clearing is performed.
 * Error precedence follows field traversal and available capacity. Output must
 * not overlap the source struct. */
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
