#ifndef QUARRY_RUNTIME_C_BINARY_RECORD_H_
#define QUARRY_RUNTIME_C_BINARY_RECORD_H_

/* Quarry C runtime: generic Binary Record Format (BRF) v0.1 byte mechanics.
 *
 * This header owns only representation-neutral byte-level mechanics shared
 * across generated C files -- record header emission/parsing, Field
 * Directory emission/parsing, big-endian scalar I/O, and unsigned LEB128
 * varuint I/O -- mirroring the split already documented for the C++ runtime
 * (docs/principles.md, "Compile-Time Knowledge"; runtime/README.md).
 * Schema-specific knowledge (record IDs, field indexes, field types, enum
 * value sets) belongs to generated code, not here.
 *
 * Scope (PR-108): scalar fields only. No enum/string/bytes/array/nested
 * record support exists yet -- see docs/design/c-backend.md and
 * compiler/backend_c/README.md for the current implemented subset and the
 * roadmap for later increments. This header is not a speculative framework
 * for those future features: it exposes exactly the primitives this slice
 * needs, and is expected to grow when they are implemented, not now.
 *
 * C99. No heap allocation. Caller-owned buffers only. No global state.
 * Wire format matches docs/specifications/binary-record-format.md exactly:
 * big-endian multi-byte values (the spec's "Byte Order" section explicitly
 * requires big-endian and forbids host-endian records), unsigned LEB128
 * varuint for Field Directory fieldOffset/fieldLength.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "quarry/runtime_c/version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUARRY_C_BINARY_RECORD_HEADER_VERSION 1U
#define QUARRY_C_BINARY_RECORD_HEADER_SIZE 16U

/* Bound on Field Directory entries this runtime version encodes/decodes in
 * a single call. The wire format itself allows up to 256 declared fields
 * per record (fieldIndex is one byte); this runtime deliberately supports a
 * smaller, fixed bound so encode/decode use only small, fixed-size local
 * storage (no heap, no unbounded stack usage) rather than a full 256-entry
 * worst case. Revisit if a real schema needs more than this many present
 * fields in one record. */
#define QUARRY_C_MAX_DIRECTORY_ENTRIES 64U

typedef enum {
    QUARRY_C_STATUS_OK = 0,
    QUARRY_C_STATUS_NULL_ARGUMENT,
    QUARRY_C_STATUS_INSUFFICIENT_CAPACITY,
    QUARRY_C_STATUS_INVALID_ARGUMENT,
    QUARRY_C_STATUS_OVERFLOW,
    QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT,
    QUARRY_C_STATUS_TRUNCATED_HEADER,
    QUARRY_C_STATUS_UNSUPPORTED_VERSION,
    QUARRY_C_STATUS_UNSUPPORTED_FLAGS,
    QUARRY_C_STATUS_INVALID_HEADER,
    QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH,
    QUARRY_C_STATUS_MALFORMED_DIRECTORY,
    QUARRY_C_STATUS_MALFORMED_VARUINT,
    QUARRY_C_STATUS_DUPLICATE_FIELD,
    QUARRY_C_STATUS_UNSORTED_DIRECTORY,
    QUARRY_C_STATUS_INVALID_FIELD_RANGE,
    QUARRY_C_STATUS_OVERLAPPING_FIELD_RANGE,
    QUARRY_C_STATUS_INVALID_FIELD_LENGTH,
    QUARRY_C_STATUS_INVALID_BOOL,
    /* Schema-level statuses: the shared runtime never produces these
     * itself (it has no schema knowledge), but generated code reuses this
     * one status type rather than inventing a second, parallel enum. */
    QUARRY_C_STATUS_UNEXPECTED_RECORD_ID,
    QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE
} quarry_c_status_t;

/* ---- Bounded writer -------------------------------------------------- */

typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t length;
} quarry_c_writer_t;

static inline void quarry_c_writer_init(quarry_c_writer_t* writer, uint8_t* buffer,
                                        size_t capacity) {
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0U;
}

static inline quarry_c_status_t quarry_c_write_u8(quarry_c_writer_t* writer, uint8_t value) {
    if (writer->length >= writer->capacity) {
        return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
    }
    writer->buffer[writer->length] = value;
    writer->length += 1U;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_write_u16(quarry_c_writer_t* writer, uint16_t value) {
    quarry_c_status_t status = quarry_c_write_u8(writer, (uint8_t)((value >> 8) & 0xFFU));
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    return quarry_c_write_u8(writer, (uint8_t)(value & 0xFFU));
}

static inline quarry_c_status_t quarry_c_write_u32(quarry_c_writer_t* writer, uint32_t value) {
    quarry_c_status_t status = quarry_c_write_u16(writer, (uint16_t)((value >> 16) & 0xFFFFU));
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    return quarry_c_write_u16(writer, (uint16_t)(value & 0xFFFFU));
}

static inline quarry_c_status_t quarry_c_write_u64(quarry_c_writer_t* writer, uint64_t value) {
    quarry_c_status_t status =
        quarry_c_write_u32(writer, (uint32_t)((value >> 32) & 0xFFFFFFFFU));
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    return quarry_c_write_u32(writer, (uint32_t)(value & 0xFFFFFFFFU));
}

static inline quarry_c_status_t quarry_c_write_i8(quarry_c_writer_t* writer, int8_t value) {
    return quarry_c_write_u8(writer, (uint8_t)value);
}

static inline quarry_c_status_t quarry_c_write_i16(quarry_c_writer_t* writer, int16_t value) {
    return quarry_c_write_u16(writer, (uint16_t)value);
}

static inline quarry_c_status_t quarry_c_write_i32(quarry_c_writer_t* writer, int32_t value) {
    return quarry_c_write_u32(writer, (uint32_t)value);
}

static inline quarry_c_status_t quarry_c_write_i64(quarry_c_writer_t* writer, int64_t value) {
    return quarry_c_write_u64(writer, (uint64_t)value);
}

static inline quarry_c_status_t quarry_c_write_bool(quarry_c_writer_t* writer, bool value) {
    return quarry_c_write_u8(writer, value ? 1U : 0U);
}

/* memcpy-based bit reinterpretation, not a union or pointer cast: safe under
 * strict aliasing and does not assume any particular alignment, per this
 * runtime's "no undefined behavior from unaligned access or aliasing"
 * requirement. Real compilers optimize this to a no-op register move. */
static inline quarry_c_status_t quarry_c_write_f32(quarry_c_writer_t* writer, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return quarry_c_write_u32(writer, bits);
}

static inline quarry_c_status_t quarry_c_write_f64(quarry_c_writer_t* writer, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return quarry_c_write_u64(writer, bits);
}

static inline quarry_c_status_t quarry_c_write_varuint(quarry_c_writer_t* writer,
                                                       uint64_t value) {
    for (;;) {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7;
        if (value != 0U) {
            byte = (uint8_t)(byte | 0x80U);
        }
        const quarry_c_status_t status = quarry_c_write_u8(writer, byte);
        if (status != QUARRY_C_STATUS_OK) {
            return status;
        }
        if (value == 0U) {
            return QUARRY_C_STATUS_OK;
        }
    }
}

static inline size_t quarry_c_varuint_encoded_size(uint64_t value) {
    size_t size = 1U;
    while (value > 0x7FU) {
        value >>= 7;
        size += 1U;
    }
    return size;
}

/* ---- Bounded reader --------------------------------------------------- */

typedef struct {
    const uint8_t* buffer;
    size_t length;
    size_t offset;
} quarry_c_reader_t;

static inline void quarry_c_reader_init(quarry_c_reader_t* reader, const uint8_t* buffer,
                                        size_t length) {
    reader->buffer = buffer;
    reader->length = length;
    reader->offset = 0U;
}

static inline quarry_c_status_t quarry_c_read_u8(quarry_c_reader_t* reader, uint8_t* out_value) {
    if (reader->offset >= reader->length) {
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    *out_value = reader->buffer[reader->offset];
    reader->offset += 1U;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_u16(quarry_c_reader_t* reader,
                                                  uint16_t* out_value) {
    uint8_t high = 0U;
    uint8_t low = 0U;
    quarry_c_status_t status = quarry_c_read_u8(reader, &high);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    status = quarry_c_read_u8(reader, &low);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = (uint16_t)(((uint16_t)high << 8) | (uint16_t)low);
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_u32(quarry_c_reader_t* reader,
                                                  uint32_t* out_value) {
    uint16_t high = 0U;
    uint16_t low = 0U;
    quarry_c_status_t status = quarry_c_read_u16(reader, &high);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    status = quarry_c_read_u16(reader, &low);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = ((uint32_t)high << 16) | (uint32_t)low;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_u64(quarry_c_reader_t* reader,
                                                  uint64_t* out_value) {
    uint32_t high = 0U;
    uint32_t low = 0U;
    quarry_c_status_t status = quarry_c_read_u32(reader, &high);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    status = quarry_c_read_u32(reader, &low);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = ((uint64_t)high << 32) | (uint64_t)low;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_i8(quarry_c_reader_t* reader, int8_t* out_value) {
    uint8_t raw = 0U;
    const quarry_c_status_t status = quarry_c_read_u8(reader, &raw);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = (int8_t)raw;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_i16(quarry_c_reader_t* reader,
                                                  int16_t* out_value) {
    uint16_t raw = 0U;
    const quarry_c_status_t status = quarry_c_read_u16(reader, &raw);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = (int16_t)raw;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_i32(quarry_c_reader_t* reader,
                                                  int32_t* out_value) {
    uint32_t raw = 0U;
    const quarry_c_status_t status = quarry_c_read_u32(reader, &raw);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = (int32_t)raw;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_i64(quarry_c_reader_t* reader,
                                                  int64_t* out_value) {
    uint64_t raw = 0U;
    const quarry_c_status_t status = quarry_c_read_u64(reader, &raw);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    *out_value = (int64_t)raw;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_bool(quarry_c_reader_t* reader, bool* out_value) {
    uint8_t raw = 0U;
    const quarry_c_status_t status = quarry_c_read_u8(reader, &raw);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    if (raw != 0U && raw != 1U) {
        return QUARRY_C_STATUS_INVALID_BOOL;
    }
    *out_value = (raw != 0U);
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_f32(quarry_c_reader_t* reader, float* out_value) {
    uint32_t bits = 0U;
    const quarry_c_status_t status = quarry_c_read_u32(reader, &bits);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    memcpy(out_value, &bits, sizeof(bits));
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_f64(quarry_c_reader_t* reader, double* out_value) {
    uint64_t bits = 0U;
    const quarry_c_status_t status = quarry_c_read_u64(reader, &bits);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    memcpy(out_value, &bits, sizeof(bits));
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_read_varuint(quarry_c_reader_t* reader,
                                                      uint64_t* out_value) {
    uint64_t value = 0U;
    uint8_t shift = 0U;
    for (uint8_t index = 0U; index < 10U; ++index) {
        uint8_t byte = 0U;
        const quarry_c_status_t status = quarry_c_read_u8(reader, &byte);
        if (status != QUARRY_C_STATUS_OK) {
            return QUARRY_C_STATUS_MALFORMED_VARUINT;
        }
        const uint64_t payload = (uint64_t)(byte & 0x7FU);
        if (shift == 63U && payload > 1U) {
            return QUARRY_C_STATUS_MALFORMED_VARUINT;
        }
        value |= payload << shift;
        if ((byte & 0x80U) == 0U) {
            *out_value = value;
            return QUARRY_C_STATUS_OK;
        }
        shift = (uint8_t)(shift + 7U);
    }
    return QUARRY_C_STATUS_MALFORMED_VARUINT;
}

/* ---- Record-level assembly (header + Field Directory + payload) ------ */

/* One already-encoded field payload, described by field_index and a
 * caller-owned byte span. Generated encode functions build a small local
 * array of these (one per present field) and pass it to
 * quarry_c_encode_record(); order does not matter, this function sorts by
 * field_index itself, matching the C++ runtime's encode_record_result. */
typedef struct {
    uint8_t field_index;
    const uint8_t* bytes;
    size_t length;
} quarry_c_field_t;

/* Copies `fields` into `sorted` (both length `field_count`), sorted by
 * ascending field_index (simple insertion sort -- field_count is bounded by
 * QUARRY_C_MAX_DIRECTORY_ENTRIES, so this is always small). Returns
 * QUARRY_C_STATUS_DUPLICATE_FIELD if two fields share a field_index. */
static inline quarry_c_status_t
quarry_c_sort_fields_(const quarry_c_field_t* fields, size_t field_count,
                     quarry_c_field_t sorted[QUARRY_C_MAX_DIRECTORY_ENTRIES]) {
    for (size_t i = 0U; i < field_count; ++i) {
        sorted[i] = fields[i];
    }
    for (size_t i = 1U; i < field_count; ++i) {
        const quarry_c_field_t key = sorted[i];
        size_t j = i;
        while (j > 0U && sorted[j - 1U].field_index > key.field_index) {
            sorted[j] = sorted[j - 1U];
            --j;
        }
        sorted[j] = key;
    }
    for (size_t i = 1U; i < field_count; ++i) {
        if (sorted[i - 1U].field_index == sorted[i].field_index) {
            return QUARRY_C_STATUS_DUPLICATE_FIELD;
        }
    }
    return QUARRY_C_STATUS_OK;
}

/* Computes the exact total encoded size (header + Field Directory +
 * payload) for the given present fields, without writing anything. Callers
 * (generated `_encoded_size()` functions) use this to size an output buffer
 * before calling quarry_c_encode_record(). */
static inline quarry_c_status_t quarry_c_record_encoded_size(const quarry_c_field_t* fields,
                                                             size_t field_count,
                                                             size_t* out_size) {
    if ((fields == NULL && field_count > 0U) || out_size == NULL) {
        return QUARRY_C_STATUS_NULL_ARGUMENT;
    }
    if (field_count > QUARRY_C_MAX_DIRECTORY_ENTRIES) {
        return QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT;
    }

    quarry_c_field_t sorted[QUARRY_C_MAX_DIRECTORY_ENTRIES];
    const quarry_c_status_t sort_status = quarry_c_sort_fields_(fields, field_count, sorted);
    if (sort_status != QUARRY_C_STATUS_OK) {
        return sort_status;
    }

    size_t directory_size = 0U;
    size_t payload_size = 0U;
    for (size_t i = 0U; i < field_count; ++i) {
        directory_size += 1U; /* field_index byte */
        directory_size += quarry_c_varuint_encoded_size((uint64_t)payload_size);
        directory_size += quarry_c_varuint_encoded_size((uint64_t)sorted[i].length);
        payload_size += sorted[i].length;
    }

    *out_size = QUARRY_C_BINARY_RECORD_HEADER_SIZE + directory_size + payload_size;
    return QUARRY_C_STATUS_OK;
}

/* Encodes one complete top-level BRF v0.1 record: header, sorted Field
 * Directory, and payload, in that order. `fields` need not be pre-sorted or
 * pre-validated; duplicate field_index values are rejected. On any failure,
 * `output`'s contents are unspecified and must not be used -- callers that
 * want to avoid ever hitting QUARRY_C_STATUS_INSUFFICIENT_CAPACITY should
 * size their buffer with quarry_c_record_encoded_size() first. */
static inline quarry_c_status_t quarry_c_encode_record(uint32_t record_id,
                                                       const quarry_c_field_t* fields,
                                                       size_t field_count, uint8_t* output,
                                                       size_t output_capacity,
                                                       size_t* out_length) {
    if ((fields == NULL && field_count > 0U) || output == NULL || out_length == NULL) {
        return QUARRY_C_STATUS_NULL_ARGUMENT;
    }
    if (record_id == 0U) {
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
    if (field_count > QUARRY_C_MAX_DIRECTORY_ENTRIES) {
        return QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT;
    }

    quarry_c_field_t sorted[QUARRY_C_MAX_DIRECTORY_ENTRIES];
    const quarry_c_status_t sort_status = quarry_c_sort_fields_(fields, field_count, sorted);
    if (sort_status != QUARRY_C_STATUS_OK) {
        return sort_status;
    }

    size_t required_size = 0U;
    const quarry_c_status_t size_status =
        quarry_c_record_encoded_size(fields, field_count, &required_size);
    if (size_status != QUARRY_C_STATUS_OK) {
        return size_status;
    }
    if (required_size > output_capacity) {
        return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
    }
    if (required_size - QUARRY_C_BINARY_RECORD_HEADER_SIZE > UINT32_MAX) {
        return QUARRY_C_STATUS_OVERFLOW;
    }
    const uint32_t payload_length =
        (uint32_t)(required_size - QUARRY_C_BINARY_RECORD_HEADER_SIZE);

    quarry_c_writer_t writer;
    quarry_c_writer_init(&writer, output, output_capacity);

#define QUARRY_C_TRY_(expr)                                                                     \
    do {                                                                                        \
        const quarry_c_status_t status_ = (expr);                                               \
        if (status_ != QUARRY_C_STATUS_OK) {                                                    \
            return status_;                                                                     \
        }                                                                                       \
    } while (0)

    QUARRY_C_TRY_(quarry_c_write_u8(&writer, QUARRY_C_BINARY_RECORD_HEADER_VERSION));
    QUARRY_C_TRY_(quarry_c_write_u8(&writer, 0U)); /* flags */
    QUARRY_C_TRY_(quarry_c_write_u8(&writer, (uint8_t)field_count)); /* directoryEntryCount */
    QUARRY_C_TRY_(quarry_c_write_u8(&writer, 0U)); /* reserved0 */
    QUARRY_C_TRY_(quarry_c_write_u32(&writer, record_id));
    QUARRY_C_TRY_(quarry_c_write_u32(&writer, 0U)); /* reserved1 */
    QUARRY_C_TRY_(quarry_c_write_u32(&writer, payload_length));

    size_t running_offset = 0U;
    for (size_t i = 0U; i < field_count; ++i) {
        QUARRY_C_TRY_(quarry_c_write_u8(&writer, sorted[i].field_index));
        QUARRY_C_TRY_(quarry_c_write_varuint(&writer, (uint64_t)running_offset));
        QUARRY_C_TRY_(quarry_c_write_varuint(&writer, (uint64_t)sorted[i].length));
        running_offset += sorted[i].length;
    }
    for (size_t i = 0U; i < field_count; ++i) {
        for (size_t b = 0U; b < sorted[i].length; ++b) {
            QUARRY_C_TRY_(quarry_c_write_u8(&writer, sorted[i].bytes[b]));
        }
    }

#undef QUARRY_C_TRY_

    *out_length = writer.length;
    return QUARRY_C_STATUS_OK;
}

typedef struct {
    uint8_t field_index;
    size_t offset; /* relative to the start of the Payload */
    size_t length;
} quarry_c_directory_entry_t;

/* A structurally validated top-level BRF v0.1 record. `entries` is sorted
 * by field_index (matching wire order) and every entry's range has already
 * been checked against the Payload bounds and against every other entry for
 * overlap. Schema-specific checks (expected record_id, known field set,
 * enum value membership) remain the generated decoder's responsibility --
 * this type carries no schema knowledge. */
typedef struct {
    const uint8_t* input;
    size_t payload_offset; /* absolute offset in `input` where Payload begins */
    uint32_t record_id;
    uint8_t entry_count;
    quarry_c_directory_entry_t entries[QUARRY_C_MAX_DIRECTORY_ENTRIES];
} quarry_c_parsed_record_t;

/* Parses and structurally validates one complete top-level BRF v0.1 record.
 * On failure, `*out_error_offset` is set to the absolute byte offset within
 * `input` where the problem was detected (matching the byte-offset
 * semantics documented for the C++ runtime's DecodeError), except for
 * QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT, which is not a wire-format
 * defect and carries no offset (this runtime's own bounded-entry-count
 * limitation, not a BRF validation rule -- see QUARRY_C_MAX_DIRECTORY_ENTRIES). */
static inline quarry_c_status_t quarry_c_parse_record(const uint8_t* input, size_t input_length,
                                                      quarry_c_parsed_record_t* out_record,
                                                      size_t* out_error_offset) {
    if (input == NULL || out_record == NULL || out_error_offset == NULL) {
        return QUARRY_C_STATUS_NULL_ARGUMENT;
    }

    if (input_length < QUARRY_C_BINARY_RECORD_HEADER_SIZE) {
        *out_error_offset = 0U;
        return QUARRY_C_STATUS_TRUNCATED_HEADER;
    }

    const uint8_t version = input[0];
    const uint8_t flags = input[1];
    const uint8_t directory_entry_count = input[2];
    const uint8_t reserved0 = input[3];
    quarry_c_reader_t header_reader;
    quarry_c_reader_init(&header_reader, input + 4, 12U);
    uint32_t record_id = 0U;
    uint32_t reserved1 = 0U;
    uint32_t payload_length = 0U;
    (void)quarry_c_read_u32(&header_reader, &record_id);
    (void)quarry_c_read_u32(&header_reader, &reserved1);
    (void)quarry_c_read_u32(&header_reader, &payload_length);

    if (version != QUARRY_C_BINARY_RECORD_HEADER_VERSION) {
        *out_error_offset = 0U;
        return QUARRY_C_STATUS_UNSUPPORTED_VERSION;
    }
    if (flags != 0U) {
        *out_error_offset = 1U;
        return QUARRY_C_STATUS_UNSUPPORTED_FLAGS;
    }
    if (reserved0 != 0U) {
        *out_error_offset = 3U;
        return QUARRY_C_STATUS_INVALID_HEADER;
    }
    if (reserved1 != 0U) {
        *out_error_offset = 8U;
        return QUARRY_C_STATUS_INVALID_HEADER;
    }
    if ((uint64_t)payload_length > (uint64_t)(input_length - QUARRY_C_BINARY_RECORD_HEADER_SIZE)) {
        *out_error_offset = 12U;
        return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    if (input_length != (size_t)QUARRY_C_BINARY_RECORD_HEADER_SIZE + (size_t)payload_length) {
        *out_error_offset = 12U;
        return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    if (directory_entry_count > QUARRY_C_MAX_DIRECTORY_ENTRIES) {
        return QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT;
    }

    const uint8_t* body = input + QUARRY_C_BINARY_RECORD_HEADER_SIZE;
    const size_t body_size = (size_t)payload_length;

    quarry_c_reader_t directory_reader;
    quarry_c_reader_init(&directory_reader, body, body_size);

    quarry_c_directory_entry_t entries[QUARRY_C_MAX_DIRECTORY_ENTRIES];
    for (uint8_t index = 0U; index < directory_entry_count; ++index) {
        const size_t entry_start = directory_reader.offset;
        if (directory_reader.offset >= directory_reader.length) {
            *out_error_offset = QUARRY_C_BINARY_RECORD_HEADER_SIZE + entry_start;
            return QUARRY_C_STATUS_MALFORMED_DIRECTORY;
        }
        uint8_t field_index = 0U;
        (void)quarry_c_read_u8(&directory_reader, &field_index);

        const size_t offset_start = directory_reader.offset;
        uint64_t field_offset = 0U;
        if (quarry_c_read_varuint(&directory_reader, &field_offset) != QUARRY_C_STATUS_OK) {
            *out_error_offset = QUARRY_C_BINARY_RECORD_HEADER_SIZE + offset_start;
            return QUARRY_C_STATUS_MALFORMED_DIRECTORY;
        }
        const size_t length_start = directory_reader.offset;
        uint64_t field_length = 0U;
        if (quarry_c_read_varuint(&directory_reader, &field_length) != QUARRY_C_STATUS_OK) {
            *out_error_offset = QUARRY_C_BINARY_RECORD_HEADER_SIZE + length_start;
            return QUARRY_C_STATUS_MALFORMED_DIRECTORY;
        }

        if (index > 0U) {
            if (entries[index - 1U].field_index == field_index) {
                *out_error_offset = QUARRY_C_BINARY_RECORD_HEADER_SIZE + entry_start;
                return QUARRY_C_STATUS_DUPLICATE_FIELD;
            }
            if (entries[index - 1U].field_index > field_index) {
                *out_error_offset = QUARRY_C_BINARY_RECORD_HEADER_SIZE + entry_start;
                return QUARRY_C_STATUS_UNSORTED_DIRECTORY;
            }
        }

        entries[index].field_index = field_index;
        entries[index].offset = (size_t)field_offset;
        entries[index].length = (size_t)field_length;
    }

    const size_t payload_start = directory_reader.offset;
    const size_t payload_size = body_size - payload_start;
    const size_t payload_region_start = QUARRY_C_BINARY_RECORD_HEADER_SIZE + payload_start;

    for (uint8_t index = 0U; index < directory_entry_count; ++index) {
        const quarry_c_directory_entry_t* entry = &entries[index];
        if (entry->offset > payload_size || entry->length > payload_size ||
            entry->offset > payload_size - entry->length) {
            *out_error_offset = payload_region_start + entry->offset;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
    }
    for (uint8_t i = 0U; i < directory_entry_count; ++i) {
        const size_t i_start = entries[i].offset;
        const size_t i_end = i_start + entries[i].length;
        for (uint8_t j = (uint8_t)(i + 1U); j < directory_entry_count; ++j) {
            const size_t j_start = entries[j].offset;
            const size_t j_end = j_start + entries[j].length;
            if (i_start < j_end && j_start < i_end) {
                const size_t later_start = (i_start > j_start) ? i_start : j_start;
                *out_error_offset = payload_region_start + later_start;
                return QUARRY_C_STATUS_OVERLAPPING_FIELD_RANGE;
            }
        }
    }

    out_record->input = input;
    out_record->payload_offset = payload_region_start;
    out_record->record_id = record_id;
    out_record->entry_count = directory_entry_count;
    for (uint8_t index = 0U; index < directory_entry_count; ++index) {
        out_record->entries[index] = entries[index];
    }
    (void)payload_size;
    return QUARRY_C_STATUS_OK;
}

typedef struct {
    const uint8_t* bytes; /* points into the same buffer given to quarry_c_parse_record */
    size_t length;
    size_t byte_offset; /* absolute offset of this field's payload within the original input */
} quarry_c_field_view_t;

/* Looks up a field by index in an already-parsed record. Returns
 * QUARRY_C_STATUS_OK with *out_found = false (not an error) when the field
 * is absent -- matching BRF's "older readers ignore unknown Field Directory
 * entries" rule and the C++ runtime's find_field/std::optional-absence
 * behavior. */
static inline quarry_c_status_t quarry_c_find_field(const quarry_c_parsed_record_t* record,
                                                    uint8_t field_index,
                                                    quarry_c_field_view_t* out_field,
                                                    bool* out_found) {
    if (record == NULL || out_field == NULL || out_found == NULL) {
        return QUARRY_C_STATUS_NULL_ARGUMENT;
    }
    for (uint8_t index = 0U; index < record->entry_count; ++index) {
        if (record->entries[index].field_index == field_index) {
            out_field->bytes = record->input + record->payload_offset + record->entries[index].offset;
            out_field->length = record->entries[index].length;
            out_field->byte_offset = record->payload_offset + record->entries[index].offset;
            *out_found = true;
            return QUARRY_C_STATUS_OK;
        }
        if (record->entries[index].field_index > field_index) {
            break;
        }
    }
    *out_found = false;
    return QUARRY_C_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* QUARRY_RUNTIME_C_BINARY_RECORD_H_ */
