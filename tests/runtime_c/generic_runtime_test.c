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
static void put32(uint8_t* p, size_t value) {
    p[0] = (uint8_t)(value >> 24U);
    p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U);
    p[3] = (uint8_t)value;
}

typedef struct {
    size_t count;
    size_t stop_after;
    quarry_brf_traversal_event_kind_t first;
    quarry_brf_traversal_event_kind_t last;
} traversal_observer_t;

static quarry_brf_traversal_control_t observe_traversal(const quarry_brf_traversal_event_t* event,
                                                        void* context);

static int deep_structure_test(void) {
    /* Exercise the production validator at the full stress depth. */
    enum { depth = 2048, storage = depth * 2, brf_capacity = depth * 25 + 21 };
    static quarry_qbs_record_view_t records[depth];
    static quarry_qbs_field_view_t fields[depth];
    static quarry_qbs_type_view_t types[depth];
    static quarry_brf_record_node_t nodes[storage];
    static quarry_brf_field_state_t states[storage];
    static uint32_t maps[storage];
    static quarry_brf_child_relation_t children[storage];
    static quarry_brf_validation_frame_t frames[storage];
    static uint8_t brf[brf_capacity];
    quarry_qbs_view_t qbs = {0};
    quarry_workspace_t workspace = {0};
    quarry_generic_limits_t limits = {0U, brf_capacity, depth * 2U, depth, 0U};
    size_t size = 26U;

    size_t child_size = 26U;
    bool child_variable = true;
    for (size_t i = depth; i-- > 0U;) {
        const bool variable = i + 1U == depth || child_variable;
        const uint32_t fixed_region = 9U;
        records[i] = (quarry_qbs_record_view_t){(uint32_t)(i + 1U),
                                                (uint32_t)i,
                                                1U,
                                                (uint8_t)variable,
                                                1U,
                                                fixed_region,
                                                (uint32_t)(variable ? 0U : (16U + fixed_region)),
                                                0U,
                                                0U};
        fields[i] = (quarry_qbs_field_view_t){
            0U, (uint16_t)i, 17U, (uint32_t)(variable ? 64U : child_size * 8U), 0U, 8U, 2U, 0U, 0U};
        types[i] = (quarry_qbs_type_view_t){(uint8_t)(i + 1U == depth ? 13U : 15U),
                                            (uint8_t)!variable,
                                            (uint16_t)(i + 1U == depth ? 0U : 0U),
                                            (uint16_t)(i + 1U < depth ? i + 1U : 0U),
                                            0U,
                                            (uint32_t)(i + 1U == depth ? 255U : 0U)};
        child_size = 25U + 1U;
        child_variable = variable;
    }
    for (size_t i = 0U; i < depth; ++i)
        if (!records[i].variable_size || types[i].fixed != 0U) {
            fprintf(stderr, "classification %zu\n", i);
            return 1;
        }
    qbs.records = records;
    qbs.record_count = depth;
    qbs.fields = fields;
    qbs.field_count = depth;
    qbs.types = types;
    qbs.type_count = depth;
    workspace.nodes = nodes;
    workspace.node_capacity = storage;
    workspace.field_states = states;
    workspace.field_state_capacity = storage;
    workspace.field_maps = maps;
    workspace.field_map_capacity = storage;
    workspace.children = children;
    workspace.child_capacity = storage;
    workspace.frames = frames;
    workspace.frame_capacity = storage;
    brf[0] = 2U;
    brf[1] = 0U;
    brf[2] = 0U;
    brf[3] = 16U;
    put32(brf + 4U, depth);
    brf[8] = 0U;
    brf[9] = 0U;
    brf[10] = 0U;
    brf[11] = 9U;
    brf[12] = 0U;
    brf[13] = 0U;
    brf[14] = 0U;
    brf[15] = 26U;
    brf[16] = 1U;
    brf[17] = 0U;
    brf[18] = 0U;
    brf[19] = 0U;
    brf[20] = 25U;
    put32(brf + 21U, 1U);
    brf[25] = 'x';
    if (brf[17] != 0U || brf[18] != 0U || brf[19] != 0U || brf[20] != 25U) {
        fprintf(stderr, "leaf descriptor\n");
        return 1;
    }
    for (size_t level = 1U; level < depth; ++level) {
        const bool variable = true;
        const size_t prefix = 25U;
        memmove(brf + prefix, brf, size);
        size += prefix;
        brf[0] = 2U;
        brf[1] = 0U;
        brf[2] = 0U;
        brf[3] = 16U;
        put32(brf + 4U, depth - level);
        brf[8] = 0U;
        brf[9] = 0U;
        brf[10] = 0U;
        brf[11] = 9U;
        put32(brf + 12U, size);
        brf[16] = 1U;
        if (variable) {
            brf[17] = 0U;
            brf[18] = 0U;
            brf[19] = 0U;
            brf[20] = 25U;
            put32(brf + 21U, size - 25U);
        }
    }
    quarry_brf_record_view_t view;
    const quarry_generic_status_t deep_status = quarry_brf_validate_with_workspace(
        &qbs, &records[0], brf, size, &view, &workspace, &limits);
    if (deep_status != QUARRY_GENERIC_OK || workspace.node_count != depth ||
        workspace.frame_high_water != depth) {
        fprintf(stderr, "deep validation %d nodes=%zu frames=%zu size=%zu caps=%zu,%zu,%zu,%zu\n",
                (int)deep_status, workspace.node_count, workspace.frame_high_water, size,
                workspace.node_capacity, workspace.field_state_capacity,
                workspace.field_map_capacity, workspace.frame_capacity);
        return 1;
    }
    quarry_string_view_t text = {0};
    quarry_brf_record_view_t current = view;
    for (size_t i = 0U; i < depth - 1U; ++i) {
        quarry_brf_record_view_t next;
        if (quarry_brf_get_record(&current, 0U, &next) != QUARRY_GENERIC_OK) {
            fprintf(stderr, "deep child access %zu\n", i);
            return 1;
        }
        current = next;
    }
    if (quarry_brf_get_string(&current, 0U, &text) != QUARRY_GENERIC_OK || text.size != 1U ||
        text.data[0] != 'x') {
        fprintf(stderr, "deep leaf access\n");
        return 1;
    }
    static quarry_brf_traversal_frame_t traversal_frames[depth];
    quarry_brf_traversal_workspace_t traversal_workspace = {traversal_frames, depth, 0U, 0U};
    quarry_brf_traversal_limits_t traversal_limits = {depth * 4U, depth};
    traversal_observer_t traversal_observer = {0U, 0U, QUARRY_BRF_EVENT_FIELD,
                                               QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&view, observe_traversal, &traversal_observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_COMPLETED ||
        traversal_workspace.frame_high_water != depth)
        return 1;
    traversal_workspace.frame_capacity = depth - 1U;
    if (quarry_brf_traverse(&view, observe_traversal, &traversal_observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_WORKSPACE_EXHAUSTED)
        return 1;

    quarry_generic_limits_t low = limits;
    low.max_nested_records = depth - 1U;
    if (quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view, &workspace, &low) !=
        QUARRY_GENERIC_RESOURCE_LIMIT) {
        fprintf(stderr, "nested limit\n");
        return 1;
    }
    low = limits;
    low.max_work_items = depth * 2U - 1U;
    if (quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view, &workspace, &low) !=
        QUARRY_GENERIC_RESOURCE_LIMIT) {
        fprintf(stderr, "work limit\n");
        return 1;
    }
    quarry_generic_status_t capacity_status;
    workspace.frame_capacity = depth;
    workspace.node_capacity = depth;
    workspace.field_state_capacity = depth;
    workspace.field_map_capacity = depth;
    workspace.child_capacity = depth - 1U;
    capacity_status = quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view,
                                                         &workspace, &limits);
    if (capacity_status != QUARRY_GENERIC_OK) {
        fprintf(stderr, "child exact capacity %d\n", (int)capacity_status);
        return 1;
    }
    workspace.child_capacity = depth - 2U;
    capacity_status = quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view,
                                                         &workspace, &limits);
    if (capacity_status != QUARRY_GENERIC_WORKSPACE_EXHAUSTED) {
        fprintf(stderr, "child capacity %d\n", (int)capacity_status);
        return 1;
    }
    workspace.child_capacity = depth;
    workspace.field_state_capacity = depth - 1U;
    capacity_status = quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view,
                                                         &workspace, &limits);
    if (capacity_status != QUARRY_GENERIC_WORKSPACE_EXHAUSTED) {
        fprintf(stderr, "state capacity %d\n", (int)capacity_status);
        return 1;
    }
    workspace.field_state_capacity = depth;
    workspace.field_map_capacity = depth - 1U;
    if (quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view, &workspace,
                                           &limits) != QUARRY_GENERIC_WORKSPACE_EXHAUSTED) {
        fprintf(stderr, "map capacity\n");
        return 1;
    }
    workspace.frame_capacity = depth - 1U;
    if (quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view, &workspace,
                                           &limits) != QUARRY_GENERIC_WORKSPACE_EXHAUSTED) {
        fprintf(stderr, "frame capacity\n");
        return 1;
    }
    workspace.frame_capacity = storage;
    workspace.node_capacity = depth - 1U;
    if (quarry_brf_validate_with_workspace(&qbs, &records[0], brf, size, &view, &workspace,
                                           &limits) != QUARRY_GENERIC_WORKSPACE_EXHAUSTED) {
        fprintf(stderr, "node capacity\n");
        return 1;
    }
    return 0;
}

static int expect(quarry_generic_status_t actual, quarry_generic_status_t wanted) {
    if (actual != wanted)
        (void)fprintf(stderr, "status %d expected %d\n", (int)actual, (int)wanted);
    return actual == wanted ? 0 : 1;
}

static size_t decode_hex(uint8_t* out, const char* text) {
    size_t count = 0U;
    while (text[0] != '\0' && text[1] != '\0') {
        unsigned value = 0U;
        if (sscanf(text, "%2x", &value) != 1)
            return 0U;
        out[count++] = (uint8_t)value;
        text += 2;
    }
    return count;
}

static int shared_mutation_test(const char* directory, const uint8_t* qbs, size_t qbs_size,
                                const uint8_t* brf, size_t brf_size, quarry_qbs_view_t* schema,
                                const quarry_qbs_record_view_t* parent,
                                quarry_workspace_t* workspace,
                                const quarry_generic_limits_t* limits) {
    char path[512];
    char line[256];
    FILE* file;
    (void)snprintf(path, sizeof(path), "%s/mutations.txt", directory);
    file = fopen(path, "r");
    if (file == NULL)
        return 1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char kind[8], name[64], replacement[160];
        unsigned offset = 0U;
        uint8_t mutated[2048];
        uint8_t replacement_bytes[80];
        size_t replacement_size;
        quarry_brf_record_view_t rejected_view;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%7[^|]|%63[^|]|%u|%159s", kind, name, &offset, replacement) != 4)
            return 1;
        if (strcmp(replacement, "TRUNCATE:32") == 0)
            replacement_size = 0U;
        else
            replacement_size = decode_hex(replacement_bytes, replacement);
        if (strcmp(kind, "BRF") == 0) {
            if (brf_size > sizeof(mutated) || offset > brf_size ||
                offset + replacement_size > brf_size)
                return 1;
            memcpy(mutated, brf, brf_size);
            memcpy(mutated + offset, replacement_bytes, replacement_size);
            quarry_workspace_reset(workspace);
            if (quarry_brf_validate_with_workspace(schema, parent, mutated, brf_size,
                                                   &rejected_view, workspace,
                                                   limits) == QUARRY_GENERIC_OK) {
                fprintf(stderr, "shared BRF mutation accepted: %s\n", name);
                (void)fclose(file);
                return 1;
            }
        } else if (strcmp(kind, "QBS") == 0) {
            size_t mutated_size = replacement_size == 0U ? offset : qbs_size;
            if (qbs_size > sizeof(mutated) || mutated_size > sizeof(mutated) ||
                offset + replacement_size > qbs_size)
                return 1;
            memcpy(mutated, qbs, qbs_size);
            memcpy(mutated + offset, replacement_bytes, replacement_size);
            quarry_workspace_reset(workspace);
            if (quarry_qbs_parse(mutated, mutated_size, schema, workspace, limits) ==
                QUARRY_GENERIC_OK) {
                fprintf(stderr, "shared QBS mutation accepted: %s\n", name);
                (void)fclose(file);
                return 1;
            }
        }
    }
    (void)fclose(file);
    return 0;
}

static quarry_brf_traversal_control_t observe_traversal(const quarry_brf_traversal_event_t* event,
                                                        void* context) {
    traversal_observer_t* observer = (traversal_observer_t*)context;
    if (observer->count == 0U)
        observer->first = event->kind;
    observer->last = event->kind;
    ++observer->count;
    return observer->stop_after != 0U && observer->count == observer->stop_after
               ? QUARRY_BRF_TRAVERSAL_STOP
               : QUARRY_BRF_TRAVERSAL_CONTINUE;
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
    if (qread != 0 || bread != 0) {
        fprintf(stderr, "fixture read q=%d b=%d path=%s\n", qread, bread, qbs_path);
        return 1;
    }
    if (deep_structure_test() != 0)
        return 1;
    quarry_qbs_record_view_t records[4];
    quarry_qbs_field_view_t fields[32];
    quarry_qbs_type_view_t types[32];
    quarry_qbs_enum_view_t enums[4];
    uint64_t enum_values[8];
    quarry_brf_record_node_t nodes[64];
    quarry_brf_field_state_t field_states[256];
    uint32_t field_maps[256], array_elements[64];
    quarry_brf_child_relation_t children[64];
    quarry_brf_record_array_relation_t arrays[32];
    quarry_brf_validation_frame_t frames[64];
    quarry_workspace_t workspace = {
        records,    4U,          fields,   32U,   types,  32U,          enums,
        4U,         enum_values, 8U,       nodes, 64U,    field_states, 256U,
        field_maps, 256U,        children, 64U,   arrays, 32U,          array_elements,
        64U,        frames,      64U,      0U,    0U,     0U,           0U,
        0U,         0U,          0U};
    quarry_qbs_view_t schema;
    quarry_generic_limits_t limits = {1024U * 1024U, 1024U * 1024U, 1024U, 64U, 1024U};
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
    quarry_brf_record_view_t structural_record;
    if (quarry_brf_validate_with_workspace(&schema, parent, brf, brf_size, &structural_record,
                                           &workspace, &limits) != QUARRY_GENERIC_OK) {
        fprintf(stderr, "structural validation failed\n");
        return 1;
    }
    quarry_brf_traversal_frame_t traversal_frames[64];
    quarry_brf_traversal_workspace_t traversal_workspace = {traversal_frames, 64U, 0U, 0U};
    quarry_brf_traversal_limits_t traversal_limits = {1024U, 64U};
    traversal_observer_t observer = {0U, 0U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_COMPLETED ||
        observer.count == 0U || observer.first != QUARRY_BRF_EVENT_RECORD_BEGIN ||
        observer.last != QUARRY_BRF_EVENT_RECORD_END)
        return 1;
    const size_t traversal_count = observer.count;
    traversal_limits.max_work_items = traversal_count;
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    observer = (traversal_observer_t){0U, 0U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_COMPLETED)
        return 1;
    traversal_limits.max_work_items = traversal_count - 1U;
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    observer = (traversal_observer_t){0U, 0U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_WORK_LIMIT)
        return 1;
    traversal_limits.max_work_items = 1024U;
    traversal_limits.max_depth = 2U;
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    observer = (traversal_observer_t){0U, 0U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_COMPLETED)
        return 1;
    traversal_limits.max_depth = 1U;
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    observer = (traversal_observer_t){0U, 0U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT)
        return 1;
    traversal_limits = (quarry_brf_traversal_limits_t){0U, 64U};
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_WORK_LIMIT)
        return 1;
    traversal_limits = (quarry_brf_traversal_limits_t){1024U, 0U};
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_DEPTH_LIMIT)
        return 1;
    traversal_limits.max_depth = 64U;
    traversal_limits.max_work_items = 1024U;
    observer = (traversal_observer_t){0U, 3U, QUARRY_BRF_EVENT_FIELD, QUARRY_BRF_EVENT_FIELD};
    quarry_brf_traversal_workspace_reset(&traversal_workspace);
    if (quarry_brf_traverse(&structural_record, observe_traversal, &observer, &traversal_workspace,
                            &traversal_limits) != QUARRY_BRF_TRAVERSAL_STOPPED ||
        observer.count != 3U || traversal_count == 3U || workspace.node_count != 6U)
        return 1;
    quarry_brf_array_view_t samples;
    if (quarry_brf_get_array(&structural_record, 8U, &samples) != QUARRY_GENERIC_OK ||
        samples.count != 3U)
        return 1;
    uint64_t sample = 0U;
    if (quarry_brf_array_get_uint(&structural_record, &samples, 2U, &sample) != QUARRY_GENERIC_OK ||
        sample != 3U)
        return 1;
    quarry_brf_array_view_t empty_samples;
    if (quarry_brf_get_array(&structural_record, 12U, &empty_samples) != QUARRY_GENERIC_OK ||
        empty_samples.count != 0U)
        return 1;
    bool structural_present = true;
    if (quarry_brf_field_is_present(&structural_record, 11U, &structural_present) !=
            QUARRY_GENERIC_OK ||
        structural_present)
        return 1;
    quarry_brf_record_view_t child_record;
    if (quarry_brf_get_record(&structural_record, 9U, &child_record) != QUARRY_GENERIC_OK)
        return 1;
    if (quarry_brf_get_uint(&child_record, 0U, &sample) != QUARRY_GENERIC_OK || sample != 100U)
        return 1;
    quarry_brf_array_view_t items;
    if (quarry_brf_get_record_array(&structural_record, 10U, &items) != QUARRY_GENERIC_OK ||
        items.count != 2U)
        return 1;
    quarry_brf_record_view_t item;
    int64_t item_value = 0;
    if (quarry_brf_record_array_get(&structural_record, &items, 1U, &item) != QUARRY_GENERIC_OK ||
        quarry_brf_get_int(&item, 0U, &item_value) != QUARRY_GENERIC_OK || item_value != 8)
        return 1;
    const quarry_generic_status_t nested_status = quarry_brf_get_record(&item, 1U, &child_record);
    const quarry_generic_status_t nested_value_status =
        nested_status == QUARRY_GENERIC_OK ? quarry_brf_get_uint(&child_record, 0U, &sample)
                                           : QUARRY_GENERIC_INVALID_ARGUMENT;
    if (nested_status != QUARRY_GENERIC_OK || nested_value_status != QUARRY_GENERIC_OK ||
        sample != 202U) {
        fprintf(stderr, "nested item child failed\n");
        return 1;
    }
    const size_t nodes_after = workspace.node_count;
    const size_t fields_after = workspace.field_state_count;
    const size_t maps_after = workspace.field_map_count;
    const size_t arrays_after = workspace.array_count;
    if (quarry_brf_get_array(&structural_record, 8U, &samples) != QUARRY_GENERIC_OK ||
        quarry_brf_get_record(&structural_record, 9U, &child_record) != QUARRY_GENERIC_OK ||
        quarry_brf_get_record_array(&structural_record, 10U, &items) != QUARRY_GENERIC_OK ||
        quarry_brf_record_array_get(&structural_record, &items, 0U, &item) != QUARRY_GENERIC_OK ||
        workspace.node_count != nodes_after || workspace.field_state_count != fields_after ||
        workspace.field_map_count != maps_after || workspace.array_count != arrays_after)
        return 1;
    quarry_generic_limits_t low_nested = limits;
    low_nested.max_nested_records = 1U;
    if (quarry_brf_validate_with_workspace(&schema, parent, brf, brf_size, &structural_record,
                                           &workspace,
                                           &low_nested) != QUARRY_GENERIC_RESOURCE_LIMIT)
        return 1;
    quarry_generic_limits_t low_elements = limits;
    low_elements.max_array_elements = 2U;
    if (quarry_brf_validate_with_workspace(&schema, parent, brf, brf_size, &structural_record,
                                           &workspace,
                                           &low_elements) != QUARRY_GENERIC_RESOURCE_LIMIT)
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
    quarry_workspace_t small_workspace = {
        records,      0U, fields,     0U, types,    0U, enums,  0U, enum_values,    0U, nodes,  0U,
        field_states, 0U, field_maps, 0U, children, 0U, arrays, 0U, array_elements, 0U, frames, 0U,
        0U,           0U, 0U,         0U, 0U,       0U, 0U};
    if (quarry_qbs_parse(qbs, qbs_size, &schema, &small_workspace, &limits) !=
        QUARRY_GENERIC_WORKSPACE_EXHAUSTED)
        return 1;
    quarry_workspace_reset(&workspace);
    if (quarry_qbs_parse(qbs, qbs_size, &schema, &workspace, &limits) != QUARRY_GENERIC_OK)
        return 1;
    if (quarry_qbs_parse(NULL, qbs_size, &schema, &workspace, &limits) !=
        QUARRY_GENERIC_INVALID_ARGUMENT)
        return 1;
    if (schema.bytes != NULL || schema.record_count != 0U)
        return 1;
    if (quarry_qbs_parse(qbs, qbs_size, &schema, &workspace, &limits) != QUARRY_GENERIC_OK)
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
    record.bytes = scalar_brf;
    record.size = 105U;
    if (quarry_brf_validate(&schema, parent, scalar_brf, 105U, &record, &limits) !=
        QUARRY_GENERIC_OK)
        return 1;
    if (expect(quarry_brf_validate(&schema, parent, scalar_brf, 105U, &record, &limits),
               QUARRY_GENERIC_OK) != 0) {
        fprintf(stderr, "brf validate failed\n");
        return 1;
    }
    if (quarry_brf_get_uint(&record, 0U, NULL) != QUARRY_GENERIC_INVALID_ARGUMENT)
        return 1;
    if (quarry_brf_get_string(&record, 6U, NULL) != QUARRY_GENERIC_INVALID_ARGUMENT)
        return 1;
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
    if (shared_mutation_test(directory, qbs, qbs_size, brf, brf_size, &schema, parent, &workspace,
                             &limits) != 0)
        return 1;
    free(scalar_brf);
    free(brf);
    free(qbs);
    return 0;
}
