#include "benchmark/workload.generated.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
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
std::vector<std::byte> bytes(const std::string& value) {
    std::vector<std::byte> result;
    if (!present(value)) return result;
    for (std::size_t index = 0; index < value.size(); index += 2)
        result.push_back(static_cast<std::byte>(std::stoul(value.substr(index, 2), nullptr, 16)));
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
benchmark::workload::shared::Child make_child(const std::string& value) {
    const auto values = split(value, ',');
    if (values.size() != 3U) throw std::runtime_error("invalid child value");
    benchmark::workload::shared::ChildBuilder builder;
    builder.set_id(number(values[0])); builder.set_label(values[1]); builder.set_payload(bytes(values[2]));
    return builder.build();
}
benchmark::workload::Workload make_record(const Row& row) {
    const auto& f = row.fields; benchmark::workload::WorkloadBuilder builder;
    if (present(f[0])) builder.set_sequence(number(f[0]));
    if (present(f[1])) builder.set_timestamp(number64(f[1]));
    if (present(f[2])) builder.set_counter(number64(f[2]));
    if (present(f[3])) builder.set_ratio(std::stof(f[3]));
    if (present(f[4])) builder.set_enabled(f[4] == "1");
    if (present(f[5])) builder.set_status(static_cast<benchmark::workload::Status>(number(f[5])));
    if (present(f[6])) builder.set_name(f[6]);
    if (present(f[7])) builder.set_payload(bytes(f[7]));
    if (present(f[8])) { std::vector<std::uint32_t> values; for (const auto& v : split(f[8], ',')) values.push_back(number(v)); builder.set_values(values); }
    if (present(f[9])) builder.set_child(make_child(f[9]));
    if (present(f[10])) { std::vector<benchmark::workload::shared::Child> values; for (const auto& v : split(f[10], ';')) values.push_back(make_child(v)); builder.set_children(values); }
    return builder.build();
}
std::vector<std::byte> encode_value(const benchmark::workload::Workload& value) {
    const auto result = benchmark::workload::encode(value);
    if (!result.has_value()) throw std::runtime_error("workload encode failed");
    return result.value();
}
double median(std::vector<double> values) { std::sort(values.begin(), values.end()); return values[values.size() / 2U]; }
}

int main(int argc, char** argv) {
    std::string dataset, operation = "round_trip", output, benchmark_case = "telemetry";
    std::uint32_t warmup = 2U, iterations = 10U, samples = 5U;
    for (int index = 1; index < argc; ++index) {
        if (index + 1 >= argc) return 1;
        const std::string option = argv[index];
        if (option == "--dataset") dataset = argv[++index];
        else if (option == "--operation") operation = argv[++index];
        else if (option == "--output") output = argv[++index];
        else if (option == "--case") benchmark_case = argv[++index];
        else if (option == "--warmup") warmup = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else if (option == "--iterations") iterations = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else if (option == "--samples") samples = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        else return 1;
    }
    if (dataset.empty() || output.empty() || iterations == 0U || samples == 0U || samples > 5U ||
        (operation != "encode" && operation != "decode" && operation != "round_trip")) return 1;
    try {
        const auto rows = load(dataset); std::vector<benchmark::workload::Workload> records;
        for (const auto& row : rows) records.push_back(make_record(row));
        std::vector<std::vector<std::byte>> encoded; for (const auto& record : records) encoded.push_back(encode_value(record));
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto decoded = benchmark::workload::decode_Workload(encoded[index]);
            if (!decoded.has_value() || !decoded->has_sequence() || *decoded->sequence() != *records[index].sequence())
                throw std::runtime_error("workload validation failed");
        }
        std::uint64_t checksum = 0; std::vector<double> durations;
        const auto run_operation = [&] {
            if (operation == "encode" || operation == "round_trip") for (const auto& record : records) checksum ^= encode_value(record).size();
            if (operation == "decode" || operation == "round_trip") for (const auto& value : encoded) {
                const auto decoded = benchmark::workload::decode_Workload(value);
                checksum ^= decoded->has_sequence() ? *decoded->sequence() : 0U;
            }
        };
        for (std::uint32_t index = 0; index < warmup; ++index) run_operation();
        for (std::uint32_t sample = 0; sample < samples; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            for (std::uint32_t index = 0; index < iterations; ++index) run_operation();
            durations.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()));
        }
        const double operations = static_cast<double>(records.size()) * iterations;
        const double latency = median(durations) / operations;
        std::ofstream result(output); result << std::setprecision(17)
            << "{\n  \"format_version\": 1,\n  \"benchmark_case\": \"" << benchmark_case
            << "\",\n  \"backend\": \"cpp\",\n  \"language\": \"C++\",\n  \"operation\": \"" << operation
            << "\",\n  \"schema_identity\": \"benchmark.workload.Workload\",\n  \"dataset_seed\": 153,\n  \"record_count\": " << records.size()
            << ",\n  \"warmup_iterations\": " << warmup << ",\n  \"measured_iterations\": " << iterations
            << ",\n  \"sample_count\": " << samples << ",\n  \"sample_durations_ns\": [";
        for (std::size_t index = 0; index < durations.size(); ++index) result << (index ? ", " : "") << durations[index];
        result << "],\n  \"operation_count\": " << static_cast<std::uint64_t>(operations * samples)
            << ",\n  \"latency_ns_per_operation\": " << latency << ",\n  \"throughput_operations_per_second\": " << 1e9 / latency
            << ",\n  \"encoded_byte_size\": " << encoded.front().size() << ",\n  \"validation_status\": \"passed\",\n  \"checksum\": " << checksum << "\n}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
