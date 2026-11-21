/* Validation-only adapter for the fixed --name-prefix rev_a experiment.
 * Both builds execute the same workload. No aliases enter generated bindings. */
#ifndef QUARRY_CORTEX_M_NAME_PREFIX_ADAPTER_H
#define QUARRY_CORTEX_M_NAME_PREFIX_ADAPTER_H
#include "rev_a/benchmark/workload.generated.h"
#define benchmark_workload_Workload_t revq0a_benchmark_workload_Workload_t
#define benchmark_workload_Workload_init revq0a_benchmark_workload_Workload_init
#define benchmark_workload_Workload_encode_result_t                                                \
    revq0a_benchmark_workload_Workload_encode_result_t
#define benchmark_workload_Workload_encode revq0a_benchmark_workload_Workload_encode
#define benchmark_workload_Workload_decode_result_t                                                \
    revq0a_benchmark_workload_Workload_decode_result_t
#define benchmark_workload_Workload_decode revq0a_benchmark_workload_Workload_decode
#define benchmark_workload_shared_Child_t revq0a_benchmark_workload_shared_Child_t
#define benchmark_workload_shared_Child_init revq0a_benchmark_workload_shared_Child_init
#define BENCHMARK_WORKLOAD_STATUS_ACTIVE revq0a_benchmark_workload_STATUS_ACTIVE
#define quarry_ns_benchmark_workload_schema_id quarry_ns_revq0a_benchmark_workload_schema_id
#define benchmark_workload_Workload_set_sequence_present                                           \
    revq0a_benchmark_workload_Workload_set_sequence_present
#define benchmark_workload_Workload_set_timestamp_present                                          \
    revq0a_benchmark_workload_Workload_set_timestamp_present
#define benchmark_workload_Workload_set_counter_present                                            \
    revq0a_benchmark_workload_Workload_set_counter_present
#define benchmark_workload_Workload_set_ratio_present                                              \
    revq0a_benchmark_workload_Workload_set_ratio_present
#define benchmark_workload_Workload_set_enabled_present                                            \
    revq0a_benchmark_workload_Workload_set_enabled_present
#define benchmark_workload_Workload_set_status_present                                             \
    revq0a_benchmark_workload_Workload_set_status_present
#define benchmark_workload_Workload_set_child_present                                              \
    revq0a_benchmark_workload_Workload_set_child_present
#define benchmark_workload_shared_Child_set_id_present                                             \
    revq0a_benchmark_workload_shared_Child_set_id_present
#endif
