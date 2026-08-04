#include "workload.pb.h"

#include <google/protobuf/arena.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Row { std::string fields[11]; };

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(delimiter, start);
        result.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

bool present(const std::string& value) { return value != "-"; }
std::uint32_t number(const std::string& value) { return static_cast<std::uint32_t>(std::stoul(value)); }
std::uint64_t number64(const std::string& value) { return static_cast<std::uint64_t>(std::stoull(value)); }

std::string bytes(const std::string& value) {
    std::string result;
    if (!present(value)) return result;
    for (std::size_t index = 0; index < value.size(); index += 2)
        result.push_back(static_cast<char>(std::stoul(value.substr(index, 2), nullptr, 16)));
    return result;
}

std::vector<Row> load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("unable to open dataset");
    std::vector<Row> rows; std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("sequence|", 0) == 0) continue;
        const auto values = split(line, '|');
        if (values.size() != 11U) throw std::runtime_error("invalid workload dataset row");
        Row row; std::copy(values.begin(), values.end(), row.fields); rows.push_back(std::move(row));
    }
    if (rows.empty()) throw std::runtime_error("empty workload dataset");
    return rows;
}

void set_child(const std::string& value, benchmark::workload::Child* child) {
    const auto fields = split(value, ',');
    if (fields.size() != 3U) throw std::runtime_error("invalid child value");
    child->set_id(number(fields[0]));
    child->set_label(fields[1]);
    child->set_payload(bytes(fields[2]));
}

benchmark::workload::Workload make_record(const Row& row) {
    const auto& f = row.fields;
    benchmark::workload::Workload result;
    if (present(f[0])) result.set_sequence(number(f[0]));
    if (present(f[1])) result.set_timestamp(number64(f[1]));
    if (present(f[2])) result.set_counter(number64(f[2]));
    if (present(f[3])) result.set_ratio(std::stof(f[3]));
    if (present(f[4])) result.set_enabled(f[4] == "1");
    if (present(f[5])) result.set_status(static_cast<benchmark::workload::Status>(number(f[5])));
    if (present(f[6])) result.set_name(f[6]);
    if (present(f[7])) result.set_payload(bytes(f[7]));
    if (present(f[8])) for (const auto& value : split(f[8], ',')) result.add_values(number(value));
    if (present(f[9])) set_child(f[9], result.mutable_child());
    if (present(f[10])) for (const auto& value : split(f[10], ';')) set_child(value, result.add_children());
    return result;
}

std::vector<std::string> encode(const std::vector<benchmark::workload::Workload>& records) {
    std::vector<std::string> result;
    result.reserve(records.size());
    for (const auto& record : records) {
        std::string value;
        if (!record.SerializeToString(&value)) throw std::runtime_error("protobuf encode failed");
        result.push_back(std::move(value));
    }
    return result;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

template <typename Operation>
std::vector<double> measure(std::uint32_t warmup, std::uint32_t iterations,
                            std::uint32_t samples, Operation operation) {
    for (std::uint32_t index = 0; index < warmup; ++index) operation();
    std::vector<double> durations;
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) operation();
        durations.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count()));
    }
    return durations;
}

void write_result(const std::string& output, const std::string& case_name,
                  const std::string& operation, std::size_t record_count,
                  std::uint32_t warmup, std::uint32_t iterations, std::uint32_t samples,
                  const std::vector<double>& durations, std::size_t encoded_size,
                  std::uint64_t checksum, bool arena_mode) {
    const double operations = static_cast<double>(record_count) * iterations;
    const double latency = median(durations) / operations;
    std::ofstream result(output);
    result << std::setprecision(17)
           << "{\n  \"format_version\": 2,\n  \"benchmark_case\": \"" << case_name
           << "\",\n  \"backend\": \"protobuf-cpp" << (arena_mode ? "-arena" : "")
           << "\",\n  \"language\": \"C++\",\n  \"wire_format\": \"protobuf\",\n  \"operation\": \""
           << operation << "\",\n  \"schema_identity\": \"benchmark.workload.Workload\",\n  \"dataset_seed\": 153,\n  \"record_count\": "
           << record_count << ",\n  \"warmup_iterations\": " << warmup
           << ",\n  \"measured_iterations\": " << iterations << ",\n  \"sample_count\": " << samples
           << ",\n  \"sample_durations_ns\": [";
    for (std::size_t index = 0; index < durations.size(); ++index)
        result << (index ? ", " : "") << durations[index];
    result << "],\n  \"operation_count\": " << static_cast<std::uint64_t>(operations * samples)
           << ",\n  \"latency_ns_per_operation\": " << latency
           << ",\n  \"throughput_operations_per_second\": " << 1e9 / latency
           << ",\n  \"encoded_byte_size\": " << encoded_size
           << ",\n  \"resources\": {\"encoded_bytes\": " << encoded_size
           << ", \"object_size\": " << sizeof(benchmark::workload::Workload)
           << ", \"allocations\": null, \"allocated_bytes\": null},"
           << "\n  \"protobuf_runtime_version\": " << GOOGLE_PROTOBUF_VERSION
           << ",\n  \"arena_mode\": " << (arena_mode ? "true" : "false")
           << ",\n  \"validation_status\": \"passed\",\n  \"checksum\": " << checksum << "\n}\n";
}
} // namespace

int main(int argc, char** argv) {
    std::string dataset, operation = "round_trip", output, case_name = "telemetry";
    std::uint32_t warmup = 2U, iterations = 10U, samples = 5U;
    bool arena_mode = false;
#ifdef QUARRY_PROTOBUF_ARENA_DEFAULT
    arena_mode = true;
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--arena") { arena_mode = true; continue; }
        if (index + 1 >= argc) return 1;
        if (option == "--dataset") dataset = argv[++index];
        else if (option == "--operation") operation = argv[++index];
        else if (option == "--output") output = argv[++index];
        else if (option == "--case") case_name = argv[++index];
        else if (option == "--warmup") warmup = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else if (option == "--iterations") iterations = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else if (option == "--samples") samples = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else return 1;
    }
    if (dataset.empty() || output.empty() || iterations == 0U || samples == 0U || samples > 5U ||
        (operation != "encode" && operation != "decode" && operation != "round_trip")) return 1;
    try {
        std::vector<benchmark::workload::Workload> records;
        for (const auto& row : load(dataset)) records.push_back(make_record(row));
        const auto encoded = encode(records);
        for (std::size_t index = 0; index < records.size(); ++index) {
            benchmark::workload::Workload decoded;
            if (!decoded.ParseFromString(encoded[index]) || decoded.sequence() != records[index].sequence())
                throw std::runtime_error("protobuf validation failed");
        }
        std::uint64_t checksum = 0;
        const auto operation_body = [&] {
            if (arena_mode) {
                google::protobuf::Arena arena;
                if (operation == "encode" || operation == "round_trip") for (const auto& record : records) {
                    auto* value = google::protobuf::Arena::Create<benchmark::workload::Workload>(&arena);
                    value->CopyFrom(record);
                    std::string encoded_value;
                    if (!value->SerializeToString(&encoded_value)) throw std::runtime_error("protobuf arena encode failed");
                    checksum ^= encoded_value.size();
                }
                if (operation == "decode" || operation == "round_trip") for (const auto& value : encoded) {
                    auto* decoded = google::protobuf::Arena::Create<benchmark::workload::Workload>(&arena);
                    if (!decoded->ParseFromString(value)) throw std::runtime_error("protobuf arena decode failed");
                    checksum ^= decoded->sequence();
                }
            } else {
                if (operation == "encode" || operation == "round_trip") for (const auto& record : records) {
                    std::string encoded_value;
                    if (!record.SerializeToString(&encoded_value)) throw std::runtime_error("protobuf encode failed");
                    checksum ^= encoded_value.size();
                }
                if (operation == "decode" || operation == "round_trip") for (const auto& value : encoded) {
                    benchmark::workload::Workload decoded;
                    if (!decoded.ParseFromString(value)) throw std::runtime_error("protobuf decode failed");
                    checksum ^= decoded.sequence();
                }
            }
        };
        const auto durations = measure(warmup, iterations, samples, operation_body);
        write_result(output, case_name, operation, records.size(), warmup, iterations, samples,
                     durations, encoded.front().size(), checksum, arena_mode);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
