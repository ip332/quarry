#define _POSIX_C_SOURCE 200809L

#include "benchmark/proof.generated.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ROWS 1024U
#define MAX_ENCODED 4096U
#define SAMPLE_COUNT 5U

typedef struct {
    uint32_t sequence;
    int enabled;
    float ratio;
    char label[33];
    uint32_t label_length;
    uint8_t payload[16];
    uint32_t payload_length;
    uint32_t readings[4];
    uint32_t readings_count;
} Row;

static double now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000000000.0 + (double)value.tv_nsec;
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int parse_hex(const char* text, uint8_t* output, uint32_t* length) {
    size_t size = strlen(text);
    if ((size & 1U) != 0U || size / 2U > 16U) return 0;
    *length = (uint32_t)(size / 2U);
    for (size_t index = 0; index < size / 2U; ++index) {
        int high = hex_value(text[index * 2U]);
        int low = hex_value(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return 0;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int load_dataset(const char* path, Row* rows, uint32_t* count) {
    FILE* input = fopen(path, "r");
    char line[512];
    if (input == NULL) return 0;
    *count = 0U;
    while (fgets(line, sizeof(line), input) != NULL) {
        char* columns[6];
        char* cursor = line;
        if (line[0] == '#' || strncmp(line, "sequence|", 9U) == 0 || line[0] == '\n') continue;
        for (unsigned int column = 0U; column < 6U; ++column) {
            columns[column] = cursor;
            while (*cursor != '\0' && *cursor != '|' && *cursor != '\n') ++cursor;
            if (*cursor == '|') {
                *cursor = '\0';
                ++cursor;
            } else {
                *cursor = '\0';
                if (column != 5U) {
                    fclose(input);
                    return 0;
                }
            }
        }
        if (*count >= MAX_ROWS) {
            fclose(input);
            return 0;
        }
        Row* row = &rows[*count];
        row->sequence = (uint32_t)strtoul(columns[0], NULL, 10);
        row->enabled = strcmp(columns[1], "1") == 0;
        row->ratio = strtof(columns[2], NULL);
        row->label_length = (uint32_t)strlen(columns[3]);
        if (row->label_length > 32U) {
            fclose(input);
            return 0;
        }
        memcpy(row->label, columns[3], row->label_length);
        if (!parse_hex(columns[4], row->payload, &row->payload_length)) {
            fclose(input);
            return 0;
        }
        row->readings_count = 0U;
        if (columns[5][0] != '\0') {
            char* reading = strtok(columns[5], ",");
            while (reading != NULL && row->readings_count < 4U) {
                row->readings[row->readings_count++] = (uint32_t)strtoul(reading, NULL, 10);
                reading = strtok(NULL, ",");
            }
        }
        ++*count;
    }
    fclose(input);
    return *count > 0U;
}

static void populate(const Row* row, benchmark_proof_Sample_t* sample) {
    benchmark_proof_Sample_init(sample);
    sample->has_sequence = true;
    sample->sequence = row->sequence;
    sample->has_enabled = true;
    sample->enabled = row->enabled;
    sample->has_ratio = true;
    sample->ratio = row->ratio;
    sample->has_label = true;
    memcpy(sample->label, row->label, row->label_length);
    sample->label_length = row->label_length;
    sample->has_payload = true;
    memcpy(sample->payload, row->payload, row->payload_length);
    sample->payload_length = row->payload_length;
    sample->has_readings = true;
    memcpy(sample->readings, row->readings, row->readings_count * sizeof(uint32_t));
    sample->readings_count = row->readings_count;
}

static int encode_rows(const Row* rows, uint32_t count, uint8_t encoded[MAX_ROWS][MAX_ENCODED],
                       size_t sizes[MAX_ROWS], uint64_t* checksum) {
    for (uint32_t index = 0; index < count; ++index) {
        benchmark_proof_Sample_t sample;
        populate(&rows[index], &sample);
        benchmark_proof_Sample_encode_result_t result =
            benchmark_proof_Sample_encode(&sample, encoded[index], MAX_ENCODED);
        if (result.status != QUARRY_C_STATUS_OK) return 0;
        sizes[index] = result.bytes_written;
        *checksum ^= sample.sequence;
    }
    return 1;
}

static int decode_rows(uint8_t encoded[MAX_ROWS][MAX_ENCODED], const size_t sizes[MAX_ROWS],
                       uint32_t count, uint64_t* checksum) {
    for (uint32_t index = 0; index < count; ++index) {
        benchmark_proof_Sample_decode_result_t result =
            benchmark_proof_Sample_decode(encoded[index], sizes[index]);
        if (result.status != QUARRY_C_STATUS_OK || !result.value.has_sequence) return 0;
        *checksum ^= result.value.sequence;
    }
    return 1;
}

static int option_value(int argc, char** argv, int* index, const char* name, const char** value) {
    if (strcmp(argv[*index], name) != 0) return 0;
    if (*index + 1 >= argc) return -1;
    *value = argv[++*index];
    return 1;
}

int main(int argc, char** argv) {
    const char* dataset = NULL;
    const char* operation = "round_trip";
    const char* output = NULL;
    uint32_t warmup = 2U;
    uint32_t iterations = 10U;
    uint32_t samples = SAMPLE_COUNT;
    for (int index = 1; index < argc; ++index) {
        const char* value = NULL;
        int matched = option_value(argc, argv, &index, "--dataset", &value);
        if (matched == 1) dataset = value;
        else if (matched == -1) return 1;
        else if ((matched = option_value(argc, argv, &index, "--operation", &value)) == 1) operation = value;
        else if (matched == -1) return 1;
        else if ((matched = option_value(argc, argv, &index, "--output", &value)) == 1) output = value;
        else if (matched == -1) return 1;
        else if ((matched = option_value(argc, argv, &index, "--warmup", &value)) == 1) warmup = (uint32_t)strtoul(value, NULL, 10);
        else if (matched == -1) return 1;
        else if ((matched = option_value(argc, argv, &index, "--iterations", &value)) == 1) iterations = (uint32_t)strtoul(value, NULL, 10);
        else if (matched == -1) return 1;
        else if ((matched = option_value(argc, argv, &index, "--samples", &value)) == 1) samples = (uint32_t)strtoul(value, NULL, 10);
        else if (matched == -1) return 1;
        else return 1;
    }
    if (dataset == NULL || output == NULL || iterations == 0U || samples == 0U ||
        (strcmp(operation, "encode") != 0 && strcmp(operation, "decode") != 0 &&
         strcmp(operation, "round_trip") != 0)) return 1;

    Row rows[MAX_ROWS];
    uint32_t count = 0U;
    uint8_t encoded[MAX_ROWS][MAX_ENCODED];
    size_t sizes[MAX_ROWS];
    uint64_t checksum = 0U;
    if (!load_dataset(dataset, rows, &count) || !encode_rows(rows, count, encoded, sizes, &checksum) ||
        !decode_rows(encoded, sizes, count, &checksum)) return 1;

    double durations[SAMPLE_COUNT];
    if (samples > SAMPLE_COUNT) return 1;
    for (uint32_t iteration = 0; iteration < warmup; ++iteration) {
        if (strcmp(operation, "encode") == 0) encode_rows(rows, count, encoded, sizes, &checksum);
        else if (strcmp(operation, "decode") == 0) decode_rows(encoded, sizes, count, &checksum);
        else { encode_rows(rows, count, encoded, sizes, &checksum); decode_rows(encoded, sizes, count, &checksum); }
    }
    for (uint32_t sample = 0; sample < samples; ++sample) {
        double start = now_ns();
        for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
            if (strcmp(operation, "encode") == 0) encode_rows(rows, count, encoded, sizes, &checksum);
            else if (strcmp(operation, "decode") == 0) decode_rows(encoded, sizes, count, &checksum);
            else { encode_rows(rows, count, encoded, sizes, &checksum); decode_rows(encoded, sizes, count, &checksum); }
        }
        durations[sample] = now_ns() - start;
    }
    double sorted[SAMPLE_COUNT];
    memcpy(sorted, durations, sizeof(sorted));
    for (uint32_t left = 0; left < samples; ++left)
        for (uint32_t right = left + 1U; right < samples; ++right)
            if (sorted[right] < sorted[left]) { double temp = sorted[left]; sorted[left] = sorted[right]; sorted[right] = temp; }
    double total = 0.0;
    for (uint32_t index = 0; index < samples; ++index) total += durations[index];
    double operations = (double)count * (double)iterations;
    FILE* result = fopen(output, "w");
    if (result == NULL) return 1;
    fprintf(result, "{\n  \"format_version\": 1,\n  \"benchmark_case\": \"proof\",\n"
                   "  \"backend\": \"c\",\n  \"language\": \"C99\",\n"
                   "  \"operation\": \"%s\",\n  \"schema_identity\": \"benchmark.proof.Sample\",\n"
                   "  \"dataset_seed\": 152,\n  \"record_count\": %" PRIu32 ",\n"
                   "  \"warmup_iterations\": %" PRIu32 ",\n  \"measured_iterations\": %" PRIu32 ",\n"
                   "  \"sample_count\": %" PRIu32 ",\n  \"sample_durations_ns\": [", operation, count, warmup, iterations, samples);
    for (uint32_t index = 0; index < samples; ++index) fprintf(result, "%s%.0f", index ? ", " : "", durations[index]);
    fprintf(result, "],\n  \"operation_count\": %.0f,\n  \"latency_ns_per_operation\": %.6f,\n"
                   "  \"throughput_operations_per_second\": %.6f,\n  \"encoded_byte_size\": %zu,\n"
                   "  \"validation_status\": \"passed\",\n  \"validation_checksum\": %" PRIu64 "\n}\n",
            operations * samples, sorted[samples / 2U] / operations,
            operations * 1000000000.0 / sorted[samples / 2U], sizes[0], checksum);
    (void)total;
    fclose(result);
    return 0;
}
