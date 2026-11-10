#include "benchmark/workload.generated.hpp"

// Reuse the C harness's datasets, buffers, validation, and timing boundaries.
// The timed calls go through the C++ wrapper's native-storage overloads.
using Wrapper = quarry::cxx::benchmark_workload_Workload;
static auto wrapper_encode(const Wrapper::native_type* value, uint8_t* output, size_t capacity) {
    return Wrapper::encode(*value, {reinterpret_cast<std::byte*>(output), capacity});
}
static benchmark_workload_Workload_decode_result_t wrapper_decode(const uint8_t* input, size_t size) {
    benchmark_workload_Workload_decode_result_t result;
    const auto decoded = Wrapper::decode(result.value, {reinterpret_cast<const std::byte*>(input), size});
    result.status = static_cast<quarry_c_status_t>(decoded.status);
    result.has_byte_offset = decoded.has_byte_offset;
    result.byte_offset = decoded.byte_offset;
    return result;
}
#define QUARRY_BENCHMARK_CPP_C_STORAGE 1
#define QUARRY_BENCHMARK_BACKEND "cpp-c-storage"
#define QUARRY_BENCHMARK_LANGUAGE "C++"
#define benchmark_workload_Workload_encode wrapper_encode
#define benchmark_workload_Workload_decode wrapper_decode
#include "../c/proof_benchmark.c"
