/* Cortex-M cross-compilation smoke exercise.
 *
 * Statically constructs a benchmark.workload.Workload record (scalars, a
 * local enum, a bounded string, bounded bytes, a bounded scalar array, a
 * cross-namespace nested record, and a cross-namespace array of records),
 * then round-trips it through the generated encode/decode codec backed by
 * Quarry::runtime_c. Deliberately host-API-free: no printf, no assert(),
 * no dynamic allocation -- results land in volatile globals so a debugger
 * (or a disassembly/footprint inspection) can observe pass/fail without any
 * hosted I/O facility, and so the calls cannot be optimized away as unused.
 */

#include "benchmark/workload.generated.h"

#define QUARRY_SMOKE_BUFFER_CAPACITY 4096U

static uint8_t g_encode_buffer[QUARRY_SMOKE_BUFFER_CAPACITY];

volatile quarry_c_status_t g_encode_status;
volatile size_t g_encoded_bytes;
volatile quarry_c_status_t g_decode_status;
volatile uint32_t g_decoded_sequence;
volatile uint32_t g_decoded_child_id;
volatile uint32_t g_decoded_children_count;

static void populate_child(benchmark_workload_shared_Child_t *child, uint32_t id,
                            const char *label) {
    benchmark_workload_shared_Child_init(child);

    child->has_id = true;
    child->id = id;

    child->has_label = true;
    child->label_length = 0U;
    while (label[child->label_length] != '\0') {
        child->label[child->label_length] = label[child->label_length];
        ++child->label_length;
    }

    child->has_payload = true;
    child->payload_length = 3U;
    child->payload[0] = 0xDEU;
    child->payload[1] = 0xADU;
    child->payload[2] = 0xBEU;
}

static void populate_workload(benchmark_workload_Workload_t *record) {
    benchmark_workload_Workload_init(record);

    record->has_sequence = true;
    record->sequence = 42U;

    record->has_timestamp = true;
    record->timestamp = 1700000000ULL;

    record->has_counter = true;
    record->counter = 7ULL;

    record->has_ratio = true;
    record->ratio = 0.5f;

    record->has_enabled = true;
    record->enabled = true;

    record->has_status = true;
    record->status = BENCHMARK_WORKLOAD_STATUS_ACTIVE;

    record->has_name = true;
    record->name_length = 0U;
    {
        static const char name[] = "cortex-m4-smoke";
        while (name[record->name_length] != '\0') {
            record->name[record->name_length] = name[record->name_length];
            ++record->name_length;
        }
    }

    record->has_payload = true;
    record->payload_length = 4U;
    record->payload[0] = 0x01U;
    record->payload[1] = 0x02U;
    record->payload[2] = 0x03U;
    record->payload[3] = 0x04U;

    record->has_values = true;
    record->values_count = 3U;
    record->values[0] = 10U;
    record->values[1] = 20U;
    record->values[2] = 30U;

    record->has_child = true;
    populate_child(&record->child, 1U, "primary");

    record->has_children = true;
    record->children_count = 1U;
    populate_child(&record->children[0], 2U, "secondary");
}

int main(void) {
    static benchmark_workload_Workload_t record;
    static benchmark_workload_Workload_decode_result_t decoded;

    populate_workload(&record);

    const benchmark_workload_Workload_encode_result_t encode_result =
        benchmark_workload_Workload_encode(&record, g_encode_buffer, QUARRY_SMOKE_BUFFER_CAPACITY);
    g_encode_status = encode_result.status;
    g_encoded_bytes = encode_result.bytes_written;

    if (encode_result.status == QUARRY_C_STATUS_OK) {
        decoded = benchmark_workload_Workload_decode(g_encode_buffer, encode_result.bytes_written);
        g_decode_status = decoded.status;
        if (decoded.status == QUARRY_C_STATUS_OK) {
            g_decoded_sequence = decoded.value.sequence;
            g_decoded_child_id = decoded.value.child.id;
            g_decoded_children_count = decoded.value.children_count;
        }
    }

    for (;;) {
        /* Bare metal: main() never returns. */
    }
}
