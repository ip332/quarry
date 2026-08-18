#pragma once

/*
 * C99 BRF v2 primitives.  This header deliberately lives beside the v1
 * binary_record.h API: generated C can migrate independently while the
 * other generated backends continue to use BRF v1.
 */

#include <quarry/runtime_c/binary_record.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define QUARRY_C_BRF_V2_FORMAT_VERSION 2U
#define QUARRY_C_BRF_V2_HEADER_SIZE 16U
#define QUARRY_C_BRF_V2_DESCRIPTOR_SIZE 8U

typedef enum {
    QUARRY_C_BRF_V2_STORAGE_FIXED = 0,
    QUARRY_C_BRF_V2_STORAGE_INLINE_FIXED_NESTED = 1,
    QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR = 2
} quarry_c_brf_v2_storage_t;

typedef struct {
    uint32_t field_index;
    uint32_t presence_bit_index;
    uint32_t byte_offset;
    uint8_t bit_offset;
    uint32_t bit_width;
    uint32_t slot_size;
    quarry_c_brf_v2_storage_t storage;
} quarry_c_brf_v2_field_layout_t;

typedef struct {
    uint32_t record_id;
    uint32_t presence_bitmap_size;
    uint32_t fixed_region_size;
    uint32_t field_count;
    const quarry_c_brf_v2_field_layout_t* fields;
} quarry_c_brf_v2_record_layout_t;

typedef struct {
    uint32_t field_index;
    const uint8_t* bytes;
    size_t length;
} quarry_c_brf_v2_field_value_t;

typedef struct {
    uint8_t format_version;
    uint8_t flags;
    uint16_t header_size;
    uint32_t record_id;
    uint32_t fixed_region_size;
    uint32_t record_size;
    const uint8_t* bytes;
    size_t length;
} quarry_c_brf_v2_parsed_record_t;

typedef struct {
    const uint8_t* bytes;
    size_t length;
    size_t byte_offset;
    bool present;
} quarry_c_brf_v2_field_view_t;

static inline uint16_t quarry_c_brf_v2_read_u16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8U) | (uint16_t)p[1]);
}

static inline uint32_t quarry_c_brf_v2_read_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static inline void quarry_c_brf_v2_write_u16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8U);
    p[1] = (uint8_t)value;
}

static inline void quarry_c_brf_v2_write_u32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24U);
    p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U);
    p[3] = (uint8_t)value;
}

static inline bool quarry_c_brf_v2_present(const uint8_t* record,
                                           uint32_t field_index) {
    return (record[QUARRY_C_BRF_V2_HEADER_SIZE + field_index / 8U] &
            (uint8_t)(1U << (field_index % 8U))) != 0U;
}

static inline bool quarry_c_brf_v2_present_bit(const uint8_t* record, uint32_t bit_index) {
    return (record[QUARRY_C_BRF_V2_HEADER_SIZE + bit_index / 8U] &
            (uint8_t)(1U << (bit_index % 8U))) != 0U;
}

static inline void quarry_c_brf_v2_set_present(uint8_t* record, uint32_t field_index) {
    record[QUARRY_C_BRF_V2_HEADER_SIZE + field_index / 8U] |=
        (uint8_t)(1U << (field_index % 8U));
}

static inline void quarry_c_brf_v2_set_present_bit(uint8_t* record, uint32_t bit_index) {
    record[QUARRY_C_BRF_V2_HEADER_SIZE + bit_index / 8U] |=
        (uint8_t)(1U << (bit_index % 8U));
}

static inline void quarry_c_brf_v2_clear_present(uint8_t* record, uint32_t field_index) {
    record[QUARRY_C_BRF_V2_HEADER_SIZE + field_index / 8U] &=
        (uint8_t)~(uint8_t)(1U << (field_index % 8U));
}

static inline quarry_c_status_t quarry_c_brf_v2_record_size(
    const quarry_c_brf_v2_record_layout_t* layout,
    const quarry_c_brf_v2_field_value_t* values, size_t value_count, size_t* out_size) {
    if (layout == NULL || out_size == NULL || (value_count != 0U && values == NULL)) {
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
    size_t size = (size_t)QUARRY_C_BRF_V2_HEADER_SIZE;
    if (size > SIZE_MAX - layout->fixed_region_size) {
        return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    size += layout->fixed_region_size;
    for (size_t i = 0U; i < value_count; ++i) {
        if (values[i].bytes == NULL && values[i].length != 0U) {
            return QUARRY_C_STATUS_INVALID_ARGUMENT;
        }
        const quarry_c_brf_v2_field_layout_t* field = NULL;
        for (uint32_t j = 0U; j < layout->field_count; ++j) {
            if (layout->fields[j].field_index == values[i].field_index) {
                field = &layout->fields[j];
                break;
            }
        }
        if (field == NULL || field->storage != QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            continue;
        }
        if (size > SIZE_MAX - values[i].length) {
            return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
        }
        size += values[i].length;
    }
    if (size > UINT32_MAX) {
        return QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    *out_size = size;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_brf_v2_encode_record(
    const quarry_c_brf_v2_record_layout_t* layout, uint32_t record_id,
    const quarry_c_brf_v2_field_value_t* values, size_t value_count, uint8_t* output,
    size_t output_capacity, size_t* out_size) {
    if (layout == NULL || out_size == NULL || output == NULL || record_id != layout->record_id ||
        (value_count != 0U && values == NULL)) {
        return QUARRY_C_STATUS_INVALID_ARGUMENT;
    }
    size_t required = 0U;
    quarry_c_status_t status =
        quarry_c_brf_v2_record_size(layout, values, value_count, &required);
    if (status != QUARRY_C_STATUS_OK) {
        return status;
    }
    if (output_capacity < required) {
        return QUARRY_C_STATUS_INSUFFICIENT_CAPACITY;
    }
    memset(output, 0, required);
    output[0] = (uint8_t)QUARRY_C_BRF_V2_FORMAT_VERSION;
    quarry_c_brf_v2_write_u16(output + 2U, (uint16_t)QUARRY_C_BRF_V2_HEADER_SIZE);
    quarry_c_brf_v2_write_u32(output + 4U, record_id);
    quarry_c_brf_v2_write_u32(output + 8U, layout->fixed_region_size);
    quarry_c_brf_v2_write_u32(output + 12U, (uint32_t)required);

    const size_t fixed_end = (size_t)QUARRY_C_BRF_V2_HEADER_SIZE + layout->fixed_region_size;
    size_t tail = fixed_end;
    for (uint32_t i = 0U; i < layout->field_count; ++i) {
        const quarry_c_brf_v2_field_layout_t* field = &layout->fields[i];
        const quarry_c_brf_v2_field_value_t* value = NULL;
        for (size_t j = 0U; j < value_count; ++j) {
            if (values[j].field_index == field->field_index) {
                value = &values[j];
                break;
            }
        }
        if (value == NULL) {
            continue;
        }
        if (field->byte_offset > required || field->slot_size > required - field->byte_offset) {
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
        quarry_c_brf_v2_set_present_bit(output, field->presence_bit_index);
        uint8_t* slot = output + field->byte_offset;
        if (field->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            if (tail > UINT32_MAX || value->length > SIZE_MAX - tail ||
                tail + value->length > required) {
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            }
            quarry_c_brf_v2_write_u32(slot, (uint32_t)tail);
            quarry_c_brf_v2_write_u32(slot + 4U, (uint32_t)value->length);
            if (value->length != 0U) {
                memcpy(output + tail, value->bytes, value->length);
            }
            tail += value->length;
        } else {
            if (value->length != field->slot_size) {
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            }
            memcpy(slot, value->bytes, value->length);
        }
    }
    if (tail != required) {
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    *out_size = required;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_brf_v2_parse_record(
    const uint8_t* input, size_t input_length, const quarry_c_brf_v2_record_layout_t* layout,
    quarry_c_brf_v2_parsed_record_t* parsed, size_t* error_offset) {
    if (parsed == NULL || layout == NULL || input == NULL || input_length < 16U) {
        if (error_offset != NULL) *error_offset = 0U;
        return QUARRY_C_STATUS_INVALID_HEADER;
    }
    if (input[0] != QUARRY_C_BRF_V2_FORMAT_VERSION || input[1] != 0U ||
        quarry_c_brf_v2_read_u16(input + 2U) != QUARRY_C_BRF_V2_HEADER_SIZE) {
        if (error_offset != NULL) *error_offset = 0U;
        return QUARRY_C_STATUS_INVALID_HEADER;
    }
    const uint32_t fixed_size = quarry_c_brf_v2_read_u32(input + 8U);
    const uint32_t record_size = quarry_c_brf_v2_read_u32(input + 12U);
    const size_t fixed_end = (size_t)16U + fixed_size;
    if (fixed_size != layout->fixed_region_size || record_size != input_length ||
        fixed_end > input_length || record_size < fixed_end ||
        quarry_c_brf_v2_read_u32(input + 4U) != layout->record_id) {
        if (error_offset != NULL) *error_offset = 8U;
        return fixed_size != layout->fixed_region_size ? QUARRY_C_STATUS_INVALID_FIELD_RANGE
                                                        : QUARRY_C_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    if (layout->presence_bitmap_size != 0U && layout->field_count != 0U) {
        const uint32_t used = layout->field_count % 8U;
        if (used != 0U &&
            (input[16U + layout->presence_bitmap_size - 1U] & (uint8_t)~((1U << used) - 1U)) !=
                0U) {
            if (error_offset != NULL) *error_offset = 16U + layout->presence_bitmap_size - 1U;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
    }
    size_t tail = fixed_end;
    for (uint32_t i = 0U; i < layout->field_count; ++i) {
        const quarry_c_brf_v2_field_layout_t* field = &layout->fields[i];
        if (field->byte_offset > fixed_end || field->slot_size > fixed_end - field->byte_offset) {
            if (error_offset != NULL) *error_offset = field->byte_offset;
            return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
        }
        const bool present = quarry_c_brf_v2_present_bit(input, field->presence_bit_index);
        const uint8_t* slot = input + field->byte_offset;
        if (!present) {
            for (uint32_t j = 0U; j < field->slot_size; ++j) {
                if (slot[j] != 0U) {
                    if (error_offset != NULL) *error_offset = field->byte_offset + j;
                    return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
                }
            }
            continue;
        }
        if (field->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            const uint32_t offset = quarry_c_brf_v2_read_u32(slot);
            const uint32_t length = quarry_c_brf_v2_read_u32(slot + 4U);
            if (offset < tail || offset > record_size || length > record_size - offset ||
                offset != tail) {
                if (error_offset != NULL) *error_offset = field->byte_offset;
                return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
            }
            tail += length;
        }
    }
    if (tail != record_size) {
        if (error_offset != NULL) *error_offset = tail;
        return QUARRY_C_STATUS_INVALID_FIELD_LENGTH;
    }
    parsed->format_version = input[0];
    parsed->flags = input[1];
    parsed->header_size = quarry_c_brf_v2_read_u16(input + 2U);
    parsed->record_id = quarry_c_brf_v2_read_u32(input + 4U);
    parsed->fixed_region_size = fixed_size;
    parsed->record_size = record_size;
    parsed->bytes = input;
    parsed->length = input_length;
    return QUARRY_C_STATUS_OK;
}

static inline quarry_c_status_t quarry_c_brf_v2_find_field(
    const quarry_c_brf_v2_parsed_record_t* parsed,
    const quarry_c_brf_v2_record_layout_t* layout, uint32_t field_index,
    quarry_c_brf_v2_field_view_t* view) {
    if (parsed == NULL || layout == NULL || view == NULL) return QUARRY_C_STATUS_INVALID_ARGUMENT;
    for (uint32_t i = 0U; i < layout->field_count; ++i) {
        const quarry_c_brf_v2_field_layout_t* field = &layout->fields[i];
        if (field->field_index != field_index) continue;
        view->present = quarry_c_brf_v2_present_bit(parsed->bytes, field->presence_bit_index);
        view->byte_offset = field->byte_offset;
        if (!view->present) {
            view->bytes = NULL;
            view->length = 0U;
            return QUARRY_C_STATUS_OK;
        }
        if (field->storage == QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR) {
            const uint8_t* descriptor = parsed->bytes + field->byte_offset;
            const uint32_t offset = quarry_c_brf_v2_read_u32(descriptor);
            const uint32_t length = quarry_c_brf_v2_read_u32(descriptor + 4U);
            view->bytes = parsed->bytes + offset;
            view->length = length;
        } else {
            view->bytes = parsed->bytes + field->byte_offset;
            view->length = field->slot_size;
        }
        return QUARRY_C_STATUS_OK;
    }
    view->present = false;
    view->bytes = NULL;
    view->length = 0U;
    view->byte_offset = 0U;
    return QUARRY_C_STATUS_INVALID_FIELD_RANGE;
}
