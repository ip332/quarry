#define _POSIX_C_SOURCE 200809L
#include "benchmark/workload.generated.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ROWS 4096U
#define MAX_FIELD 8192U
#define MAX_ENCODED 16384U
#define SAMPLE_COUNT 5U
typedef struct { char fields[11][MAX_FIELD]; } Row;

static double now_ns(void) { struct timespec value; clock_gettime(CLOCK_MONOTONIC, &value); return (double)value.tv_sec * 1e9 + (double)value.tv_nsec; }
static int present(const char* value) { return strcmp(value, "-") != 0; }
static uint32_t number(const char* value) { return (uint32_t)strtoul(value, NULL, 10); }
static uint64_t number64(const char* value) { return (uint64_t)strtoull(value, NULL, 10); }
static int parse_hex(const char* text, uint8_t* output, uint32_t* length, uint32_t capacity) {
    size_t size = present(text) ? strlen(text) : 0U;
    if (size % 2U != 0U || size / 2U > capacity) return 0;
    *length = (uint32_t)(size / 2U);
    for (size_t index = 0; index < size; index += 2U) {
        char pair[3] = {text[index], text[index + 1U], '\0'};
        output[index / 2U] = (uint8_t)strtoul(pair, NULL, 16);
    }
    return 1;
}
static int split_line(char* line, Row* row) {
    char* cursor = line;
    for (unsigned index = 0; index < 11U; ++index) {
        char* separator = strchr(cursor, '|');
        if (separator != NULL) *separator = '\0';
        if (strlen(cursor) >= MAX_FIELD) return 0;
        strcpy(row->fields[index], cursor);
        if (separator == NULL) return index == 10U;
        cursor = separator + 1;
    }
    return 0;
}
static int load_dataset(const char* path, Row* rows, uint32_t* count) {
    FILE* input = fopen(path, "r"); if (input == NULL) return 0;
    char line[11U * MAX_FIELD]; *count = 0U;
    while (fgets(line, sizeof(line), input) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || strncmp(line, "sequence|", 9U) == 0 || *count >= MAX_ROWS) continue;
        line[strcspn(line, "\r\n")] = '\0';
        if (!split_line(line, &rows[*count])) { fclose(input); return 0; }
        ++*count;
    }
    fclose(input); return *count > 0U;
}
static int parse_child(const char* text, benchmark_workload_shared_Child_t* child) {
    char copy[MAX_FIELD]; char* save = NULL; char* value;
    if (strlen(text) >= sizeof(copy)) return 0;
    strcpy(copy, text);
    benchmark_workload_shared_Child_init(child);
    value = strtok_r(copy, ",", &save); if (value == NULL) return 0; child->has_id = true; child->id = number(value);
    value = strtok_r(NULL, ",", &save); if (value == NULL || strlen(value) > 64U) return 0; child->has_label = true; child->label_length = (uint32_t)strlen(value); memcpy(child->label, value, child->label_length);
    value = strtok_r(NULL, ",", &save); if (value == NULL || !parse_hex(value, child->payload, &child->payload_length, 128U)) return 0; child->has_payload = true;
    return 1;
}
static int populate(const Row* row, benchmark_workload_Workload_t* record) {
    benchmark_workload_Workload_init(record);
    if (present(row->fields[0])) { record->has_sequence = true; record->sequence = number(row->fields[0]); }
    if (present(row->fields[1])) { record->has_timestamp = true; record->timestamp = number64(row->fields[1]); }
    if (present(row->fields[2])) { record->has_counter = true; record->counter = number64(row->fields[2]); }
    if (present(row->fields[3])) { record->has_ratio = true; record->ratio = (float)strtod(row->fields[3], NULL); }
    if (present(row->fields[4])) { record->has_enabled = true; record->enabled = strcmp(row->fields[4], "1") == 0; }
    if (present(row->fields[5])) { record->has_status = true; record->status = (benchmark_workload_Status_t)number(row->fields[5]); }
    if (present(row->fields[6])) { if (strlen(row->fields[6]) > 256U) return 0; record->has_name = true; record->name_length = (uint32_t)strlen(row->fields[6]); memcpy(record->name, row->fields[6], record->name_length); }
    if (present(row->fields[7])) { record->has_payload = true; if (!parse_hex(row->fields[7], record->payload, &record->payload_length, 1024U)) return 0; }
    if (present(row->fields[8])) { char copy[MAX_FIELD]; char* save = NULL; char* value; strcpy(copy, row->fields[8]); record->has_values = true; value = strtok_r(copy, ",", &save); while (value != NULL && record->values_count < 64U) { record->values[record->values_count++] = number(value); value = strtok_r(NULL, ",", &save); } }
    if (present(row->fields[9])) { record->has_child = true; if (!parse_child(row->fields[9], &record->child)) return 0; }
    if (present(row->fields[10])) { char copy[MAX_FIELD]; char* save = NULL; char* value; strcpy(copy, row->fields[10]); record->has_children = true; value = strtok_r(copy, ";", &save); while (value != NULL && record->children_count < 8U) { if (!parse_child(value, &record->children[record->children_count])) return 0; ++record->children_count; value = strtok_r(NULL, ";", &save); } }
    return 1;
}
static int encode_rows(const benchmark_workload_Workload_t* records, uint32_t count, uint8_t encoded[MAX_ROWS][MAX_ENCODED], size_t sizes[MAX_ROWS], uint64_t* checksum) {
    for (uint32_t index = 0; index < count; ++index) { const benchmark_workload_Workload_encode_result_t result = benchmark_workload_Workload_encode(&records[index], encoded[index], MAX_ENCODED); if (result.status != QUARRY_C_STATUS_OK) return 0; sizes[index] = result.bytes_written; *checksum ^= records[index].sequence; } return 1;
}
static int decode_rows(uint8_t encoded[MAX_ROWS][MAX_ENCODED], const size_t sizes[MAX_ROWS], uint32_t count, uint64_t* checksum) {
    for (uint32_t index = 0; index < count; ++index) { const benchmark_workload_Workload_decode_result_t result = benchmark_workload_Workload_decode(encoded[index], sizes[index]); if (result.status != QUARRY_C_STATUS_OK || !result.value.has_sequence) return 0; *checksum ^= result.value.sequence; } return 1;
}
static double median(double* values, uint32_t count) { for (uint32_t left = 0; left < count; ++left) for (uint32_t right = left + 1U; right < count; ++right) if (values[right] < values[left]) { const double temp = values[left]; values[left] = values[right]; values[right] = temp; } return values[count / 2U]; }

int main(int argc, char** argv) {
    const char* dataset = NULL; const char* operation = "round_trip"; const char* output = NULL; const char* benchmark_case = "telemetry"; uint32_t warmup = 2U, iterations = 10U, samples = SAMPLE_COUNT;
    for (int index = 1; index < argc; ++index) { if (index + 1 >= argc) return 1; if (strcmp(argv[index], "--dataset") == 0) dataset = argv[++index]; else if (strcmp(argv[index], "--operation") == 0) operation = argv[++index]; else if (strcmp(argv[index], "--output") == 0) output = argv[++index]; else if (strcmp(argv[index], "--case") == 0) benchmark_case = argv[++index]; else if (strcmp(argv[index], "--warmup") == 0) warmup = number(argv[++index]); else if (strcmp(argv[index], "--iterations") == 0) iterations = number(argv[++index]); else if (strcmp(argv[index], "--samples") == 0) samples = number(argv[++index]); else return 1; }
    if (dataset == NULL || output == NULL || iterations == 0U || samples == 0U || samples > SAMPLE_COUNT || (strcmp(operation, "encode") != 0 && strcmp(operation, "decode") != 0 && strcmp(operation, "round_trip") != 0)) return 1;
    static Row rows[MAX_ROWS]; static benchmark_workload_Workload_t records[MAX_ROWS]; static uint8_t encoded[MAX_ROWS][MAX_ENCODED]; static size_t sizes[MAX_ROWS]; uint32_t count = 0U; uint64_t checksum = 0U;
    if (!load_dataset(dataset, rows, &count)) return 1;
    for (uint32_t index = 0; index < count; ++index) {
        if (!populate(&rows[index], &records[index])) return 1;
    }
    if (!encode_rows(records, count, encoded, sizes, &checksum) ||
        !decode_rows(encoded, sizes, count, &checksum)) return 1;
    double durations[SAMPLE_COUNT]; for (uint32_t sample = 0; sample < samples; ++sample) durations[sample] = 0.0;
    for (uint32_t iteration = 0; iteration < warmup; ++iteration) { if (strcmp(operation, "encode") == 0) encode_rows(records, count, encoded, sizes, &checksum); else if (strcmp(operation, "decode") == 0) decode_rows(encoded, sizes, count, &checksum); else { encode_rows(records, count, encoded, sizes, &checksum); decode_rows(encoded, sizes, count, &checksum); } }
    for (uint32_t sample = 0; sample < samples; ++sample) { const double start = now_ns(); for (uint32_t iteration = 0; iteration < iterations; ++iteration) { if (strcmp(operation, "encode") == 0) encode_rows(records, count, encoded, sizes, &checksum); else if (strcmp(operation, "decode") == 0) decode_rows(encoded, sizes, count, &checksum); else { encode_rows(records, count, encoded, sizes, &checksum); decode_rows(encoded, sizes, count, &checksum); } } durations[sample] = now_ns() - start; }
    const double operations = (double)count * (double)iterations; const double latency = median(durations, samples) / operations; FILE* result = fopen(output, "w"); if (result == NULL) return 1;
    fprintf(result, "{\n  \"format_version\": 1,\n  \"benchmark_case\": \"%s\",\n  \"backend\": \"c\",\n  \"language\": \"C99\",\n  \"operation\": \"%s\",\n  \"schema_identity\": \"benchmark.workload.Workload\",\n  \"dataset_seed\": 153,\n  \"record_count\": %" PRIu32 ",\n  \"warmup_iterations\": %" PRIu32 ",\n  \"measured_iterations\": %" PRIu32 ",\n  \"sample_count\": %" PRIu32 ",\n  \"sample_durations_ns\": [", benchmark_case, operation, count, warmup, iterations, samples);
    for (uint32_t sample = 0; sample < samples; ++sample) fprintf(result, "%s%.17g", sample ? ", " : "", durations[sample]);
    fprintf(result, "],\n  \"operation_count\": %" PRIu64 ",\n  \"latency_ns_per_operation\": %.17g,\n  \"throughput_operations_per_second\": %.17g,\n  \"encoded_byte_size\": %zu,\n  \"validation_status\": \"passed\",\n  \"checksum\": %" PRIu64 "\n}\n", (uint64_t)((double)count * iterations * samples), latency, 1e9 / latency, sizes[0], checksum);
    fclose(result); return 0;
}
