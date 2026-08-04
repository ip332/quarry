#include "benchmark/proof.generated.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace {

using Sample = benchmark::proof::Sample;
using Builder = benchmark::proof::SampleBuilder;
using Bytes = std::vector<std::byte>;
using Clock = std::chrono::steady_clock;

struct Row {
    std::uint32_t sequence;
    bool enabled;
    float ratio;
    std::string label;
    Bytes payload;
    std::vector<std::uint32_t> readings;
};

struct Options {
    std::string dataset;
    std::string operation = "round_trip";
    std::string output;
    std::uint32_t warmup = 2;
    std::uint32_t iterations = 10;
    std::uint32_t samples = 5;
};

std::vector<std::string> split(std::string_view value, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(separator, start);
        result.emplace_back(value.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

Bytes parse_hex(std::string_view value) {
    Bytes result;
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const auto nibble = [](char ch) -> unsigned int {
            if (ch >= '0' && ch <= '9') return static_cast<unsigned int>(ch - '0');
            if (ch >= 'a' && ch <= 'f') return static_cast<unsigned int>(ch - 'a' + 10);
            return static_cast<unsigned int>(ch - 'A' + 10);
        };
        result.push_back(static_cast<std::byte>((nibble(value[index]) << 4U) +
                                                 nibble(value[index + 1])));
    }
    return result;
}

std::vector<Row> load_dataset(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open dataset");
    std::vector<Row> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#' || line.rfind("sequence|", 0) == 0) continue;
        const auto columns = split(line, '|');
        if (columns.size() != 6) throw std::runtime_error("invalid dataset row");
        Row row{static_cast<std::uint32_t>(std::stoul(columns[0])), columns[1] == "1",
                std::stof(columns[2]), columns[3], parse_hex(columns[4]), {}};
        if (!columns[5].empty()) {
            for (const std::string& value : split(columns[5], ',')) {
                row.readings.push_back(static_cast<std::uint32_t>(std::stoul(value)));
            }
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty()) throw std::runtime_error("dataset is empty");
    return rows;
}

std::vector<Sample> make_samples(const std::vector<Row>& rows) {
    std::vector<Sample> samples;
    for (const Row& row : rows) {
        Builder builder;
        if (!builder.set_sequence(row.sequence) || !builder.set_enabled(row.enabled) ||
            !builder.set_ratio(row.ratio) || !builder.set_label(row.label) ||
            !builder.set_payload(row.payload) || !builder.set_readings(row.readings)) {
            throw std::runtime_error("dataset value rejected by generated API");
        }
        samples.push_back(builder.build());
    }
    return samples;
}

std::vector<Bytes> encode_all(const std::vector<Sample>& samples) {
    std::vector<Bytes> encoded;
    for (const Sample& sample : samples) {
        const auto value = benchmark::proof::encode(sample);
        if (!value.has_value()) throw std::runtime_error("generated encode failed");
        encoded.push_back(*value);
    }
    return encoded;
}

std::uint64_t consume_decode(const std::vector<Bytes>& encoded) {
    std::uint64_t checksum = 0;
    for (const Bytes& bytes : encoded) {
        const auto decoded = benchmark::proof::decode_Sample(bytes);
        if (!decoded.has_value()) throw std::runtime_error("generated decode failed");
        checksum ^= decoded->sequence() == nullptr ? 0U : *decoded->sequence();
    }
    return checksum;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto value = [&](const char* name) {
            if (index + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
            return std::string(argv[++index]);
        };
        if (argument == "--dataset") options.dataset = value("--dataset");
        else if (argument == "--operation") options.operation = value("--operation");
        else if (argument == "--output") options.output = value("--output");
        else if (argument == "--warmup") options.warmup = static_cast<std::uint32_t>(std::stoul(value("--warmup")));
        else if (argument == "--iterations") options.iterations = static_cast<std::uint32_t>(std::stoul(value("--iterations")));
        else if (argument == "--samples") options.samples = static_cast<std::uint32_t>(std::stoul(value("--samples")));
        else throw std::runtime_error("unknown option: " + argument);
    }
    if (options.dataset.empty() || options.output.empty() || options.iterations == 0 || options.samples == 0) {
        throw std::runtime_error("--dataset, --output, positive --iterations, and positive --samples are required");
    }
    if (options.operation != "encode" && options.operation != "decode" && options.operation != "round_trip") {
        throw std::runtime_error("operation must be encode, decode, or round_trip");
    }
    return options;
}

int run(const Options& options) {
    const auto rows = load_dataset(options.dataset);
    const auto samples = make_samples(rows);
    const auto encoded = encode_all(samples);
    std::uint64_t checksum = consume_decode(encoded);
    const std::function<void()> operation = [&] {
        if (options.operation == "encode") checksum ^= encode_all(samples).front().size();
        else if (options.operation == "decode") checksum ^= consume_decode(encoded);
        else checksum ^= consume_decode(encode_all(samples));
    };
    for (std::uint32_t iteration = 0; iteration < options.warmup; ++iteration) operation();

    std::vector<double> durations;
    for (std::uint32_t sample = 0; sample < options.samples; ++sample) {
        const auto start = Clock::now();
        for (std::uint32_t iteration = 0; iteration < options.iterations; ++iteration) operation();
        const auto end = Clock::now();
        durations.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }
    std::sort(durations.begin(), durations.end());
    const double median = durations[durations.size() / 2];
    const double operations = static_cast<double>(rows.size()) * options.iterations;
    std::ofstream output(options.output);
    if (!output) throw std::runtime_error("cannot open result output");
    output << "{\n  \"format_version\": 1,\n  \"benchmark_case\": \"proof\",\n"
           << "  \"backend\": \"cpp\",\n  \"language\": \"C++\",\n"
           << "  \"operation\": \"" << options.operation << "\",\n"
           << "  \"schema_identity\": \"benchmark.proof.Sample\",\n"
           << "  \"dataset_seed\": 152,\n  \"record_count\": " << rows.size() << ",\n"
           << "  \"warmup_iterations\": " << options.warmup << ",\n"
           << "  \"measured_iterations\": " << options.iterations << ",\n"
           << "  \"sample_count\": " << durations.size() << ",\n"
           << "  \"sample_durations_ns\": [";
    for (std::size_t index = 0; index < durations.size(); ++index) output << (index ? ", " : "") << durations[index];
    output << "],\n  \"operation_count\": " << static_cast<std::uint64_t>(operations * durations.size())
           << ",\n  \"latency_ns_per_operation\": " << median / operations
           << ",\n  \"throughput_operations_per_second\": " << (operations * 1e9 / median)
           << ",\n  \"encoded_byte_size\": " << encoded.front().size()
           << ",\n  \"validation_status\": \"passed\",\n  \"validation_checksum\": " << checksum << "\n}\n";
    return output.good() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "benchmark C++ error: " << error.what() << '\n';
        return 1;
    }
}
