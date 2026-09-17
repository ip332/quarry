#include "quarry/runtime_c/generic_brf.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { PRINT_RECORD = 1U, PRINT_ARRAY = 2U };

typedef struct {
    quarry_brf_print_write_callback_t write;
    void* context;
    quarry_brf_print_workspace_t* workspace;
    quarry_brf_print_options_t options;
    quarry_brf_print_result_t result;
    size_t pending_record_indent;
} print_context_t;

static int put(print_context_t* context, const char* data, size_t size) {
    if (context->result != QUARRY_BRF_PRINT_COMPLETED)
        return 0;
    if (size > context->options.max_output_bytes - context->workspace->output_bytes) {
        context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
        return 0;
    }
    if (size != 0U && context->write(data, size, context->context) != 0) {
        context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
        return 0;
    }
    context->workspace->output_bytes += size;
    return 1;
}

static int text(print_context_t* context, const char* value) {
    return put(context, value, strlen(value));
}

static int formatted(print_context_t* context, const char* format, ...) {
    char buffer[128];
    va_list arguments;
    int size;
    va_start(arguments, format);
    size = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (size < 0 || (size_t)size >= sizeof(buffer)) {
        context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
        return 0;
    }
    return put(context, buffer, (size_t)size);
}

static int indent(print_context_t* context, size_t depth) {
    const size_t width = context->options.indent_width;
    for (size_t i = 0U; i < depth; ++i)
        for (size_t j = 0U; j < width; ++j)
            if (!put(context, " ", 1U))
                return 0;
    return 1;
}

static int name_or_index(print_context_t* context, const quarry_brf_record_view_t* record,
                         uint16_t field_index) {
    quarry_string_view_t name = {0};
    const quarry_generic_status_t status =
        quarry_qbs_field_name(record->qbs, record->schema, field_index, &name);
    if (status == QUARRY_GENERIC_OK)
        return put(context, name.data, name.size);
    if (status != QUARRY_GENERIC_FIELD_ABSENT && status != QUARRY_GENERIC_FIELD_NOT_FOUND) {
        context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
        return 0;
    }
    return formatted(context, "field[%u]", (unsigned)field_index);
}

static int record_label(print_context_t* context, const quarry_brf_record_view_t* record,
                        bool array_element) {
    quarry_string_view_t name = {0};
    if (!array_element) {
        const quarry_generic_status_t status =
            quarry_qbs_record_name(record->qbs, record->schema, &name);
        if (status == QUARRY_GENERIC_OK)
            return put(context, name.data, name.size) && text(context, " {");
        if (status != QUARRY_GENERIC_FIELD_ABSENT && status != QUARRY_GENERIC_FIELD_NOT_FOUND) {
            context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
            return 0;
        }
        return formatted(context, "record[%" PRIu32 "] {", record->schema->record_id);
    }
    return text(context, "{");
}

static int string_value(print_context_t* context, quarry_string_view_t value) {
    if (!text(context, "\""))
        return 0;
    for (size_t i = 0U; i < value.size; ++i) {
        const unsigned char c = (unsigned char)value.data[i];
        if (c == '\\' || c == '"') {
            if (!put(context, "\\", 1U) || !put(context, (const char*)&c, 1U))
                return 0;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            const char escaped = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
            if (!put(context, "\\", 1U) || !put(context, &escaped, 1U))
                return 0;
        } else if (c < 0x20U) {
            if (!formatted(context, "\\u%04x", (unsigned)c))
                return 0;
        } else if (!put(context, (const char*)&c, 1U)) {
            return 0;
        }
    }
    return text(context, "\"");
}

static int scalar_value(print_context_t* context, const quarry_brf_scalar_t* value) {
    switch (value->kind) {
    case QUARRY_BRF_SCALAR_UINT: return formatted(context, "%" PRIu64, value->uint_value);
    case QUARRY_BRF_SCALAR_INT: return formatted(context, "%" PRId64, value->int_value);
    case QUARRY_BRF_SCALAR_BOOL: return text(context, value->bool_value ? "true" : "false");
    case QUARRY_BRF_SCALAR_FLOAT: return formatted(context, "%.9g", (double)value->float_value);
    case QUARRY_BRF_SCALAR_DOUBLE: return formatted(context, "%.17g", value->double_value);
    case QUARRY_BRF_SCALAR_ENUM: return formatted(context, "enum(%" PRId64 ")", value->int_value);
    case QUARRY_BRF_SCALAR_STRING: return string_value(context, value->string_value);
    case QUARRY_BRF_SCALAR_BYTES:
        if (!text(context, "0x")) return 0;
        for (size_t i = 0U; i < value->bytes_value.size; ++i)
            if (!formatted(context, "%02x", value->bytes_value.data[i])) return 0;
        return 1;
    }
    context->result = QUARRY_BRF_PRINT_OUTPUT_ERROR;
    return 0;
}

static quarry_brf_traversal_control_t print_event(const quarry_brf_traversal_event_t* event,
                                                   void* opaque) {
    print_context_t* context = (print_context_t*)opaque;
    quarry_brf_print_workspace_t* workspace = context->workspace;
    quarry_brf_print_frame_t* frame = NULL;
    if (event->kind == QUARRY_BRF_EVENT_RECORD_BEGIN) {
        const bool array_element = workspace->frame_count != 0U &&
                                   workspace->frames[workspace->frame_count - 1U].kind == PRINT_ARRAY;
        if (!record_label(context, &event->record, array_element)) return QUARRY_BRF_TRAVERSAL_STOP;
        if (workspace->frame_count >= workspace->frame_capacity) {
            context->result = QUARRY_BRF_PRINT_WORKSPACE_EXHAUSTED;
            return QUARRY_BRF_TRAVERSAL_STOP;
        }
        const size_t record_indent = workspace->frame_count == 0U ? 0U : context->pending_record_indent;
        workspace->frames[workspace->frame_count] =
            (quarry_brf_print_frame_t){PRINT_RECORD, 1U, record_indent};
        ++workspace->frame_count;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_RECORD_END) {
        frame = &workspace->frames[workspace->frame_count - 1U];
        if (frame->first != 0U) {
            if (!text(context, "}")) return QUARRY_BRF_TRAVERSAL_STOP;
        } else if (!text(context, "\n") || !indent(context, frame->indent) ||
                   !text(context, "}")) {
            return QUARRY_BRF_TRAVERSAL_STOP;
        }
        --workspace->frame_count;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_FIELD) {
        if (!event->present) return QUARRY_BRF_TRAVERSAL_CONTINUE;
        frame = &workspace->frames[workspace->frame_count - 1U];
        if (!text(context, "\n")) return QUARRY_BRF_TRAVERSAL_STOP;
        frame->first = 0U;
        if (!indent(context, frame->indent + 1U) ||
            !name_or_index(context, &event->record, event->field_index) ||
            !text(context, ": ")) return QUARRY_BRF_TRAVERSAL_STOP;
        context->pending_record_indent = frame->indent + 1U;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_ARRAY_BEGIN) {
        if (!text(context, "[") || workspace->frame_count >= workspace->frame_capacity) {
            context->result = workspace->frame_count >= workspace->frame_capacity
                                  ? QUARRY_BRF_PRINT_WORKSPACE_EXHAUSTED : context->result;
            return QUARRY_BRF_TRAVERSAL_STOP;
        }
        const size_t array_indent = workspace->frames[workspace->frame_count - 1U].indent + 1U;
        workspace->frames[workspace->frame_count] =
            (quarry_brf_print_frame_t){PRINT_ARRAY, 1U, array_indent};
        ++workspace->frame_count;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_ARRAY_ELEMENT) {
        frame = &workspace->frames[workspace->frame_count - 1U];
        if (frame->first == 0U && !text(context, ",")) return QUARRY_BRF_TRAVERSAL_STOP;
        frame->first = 0U;
        if (!text(context, "\n") || !indent(context, frame->indent + 1U))
            return QUARRY_BRF_TRAVERSAL_STOP;
        context->pending_record_indent = frame->indent + 1U;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_ARRAY_END) {
        frame = &workspace->frames[workspace->frame_count - 1U];
        if (frame->first == 0U && (!text(context, "\n") || !indent(context, frame->indent)))
            return QUARRY_BRF_TRAVERSAL_STOP;
        if (!text(context, "]")) return QUARRY_BRF_TRAVERSAL_STOP;
        --workspace->frame_count;
        return QUARRY_BRF_TRAVERSAL_CONTINUE;
    }
    if (event->kind == QUARRY_BRF_EVENT_SCALAR && !scalar_value(context, &event->scalar))
        return QUARRY_BRF_TRAVERSAL_STOP;
    return QUARRY_BRF_TRAVERSAL_CONTINUE;
}

quarry_brf_print_result_t quarry_brf_print(
    const quarry_brf_record_view_t* record, quarry_brf_print_write_callback_t write, void* output,
    quarry_brf_print_workspace_t* workspace, quarry_brf_traversal_workspace_t* traversal_workspace,
    const quarry_brf_traversal_limits_t* traversal_limits, const quarry_brf_print_options_t* options) {
    print_context_t context;
    quarry_brf_traversal_result_t traversal_result;
    if (record == NULL || write == NULL || workspace == NULL || workspace->frames == NULL ||
        workspace->frame_capacity == 0U || traversal_workspace == NULL)
        return QUARRY_BRF_PRINT_INVALID_ARGUMENT;
    workspace->frame_count = 0U;
    workspace->output_bytes = 0U;
    context = (print_context_t){write, output, workspace,
                                options == NULL ? (quarry_brf_print_options_t){2U, SIZE_MAX}
                                                 : *options,
                                QUARRY_BRF_PRINT_COMPLETED,
                                0U};
    traversal_result = quarry_brf_traverse(record, print_event, &context, traversal_workspace,
                                           traversal_limits);
    if (context.result != QUARRY_BRF_PRINT_COMPLETED)
        return context.result;
    switch (traversal_result) {
    case QUARRY_BRF_TRAVERSAL_COMPLETED: return QUARRY_BRF_PRINT_COMPLETED;
    case QUARRY_BRF_TRAVERSAL_WORK_LIMIT: return QUARRY_BRF_PRINT_WORK_LIMIT;
    case QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT: return QUARRY_BRF_PRINT_DEPTH_LIMIT;
    case QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED: return QUARRY_BRF_PRINT_WORKSPACE_EXHAUSTED;
    default: return QUARRY_BRF_PRINT_INVALID_ARGUMENT;
    }
}
