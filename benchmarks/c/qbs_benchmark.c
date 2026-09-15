#define _POSIX_C_SOURCE 200809L
#include "benchmark/workload.generated.h"
#include "quarry/runtime_c/generic_brf.h"
#include "quarry/runtime_c/generic_brf_encoding.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QBS_BYTES 4096U
#define BRF_BYTES 16384U
#define N 5U
typedef struct { benchmark_workload_shared_Child_t* child; } ChildCtx;
typedef struct { benchmark_workload_shared_Child_t* items; size_t count; } ChildArrayCtx;
typedef struct { uint32_t* values; size_t count; } UintArrayCtx;
typedef struct { benchmark_workload_Workload_t* record; } RootCtx;

static quarry_generic_status_t child_field(const quarry_brf_record_provider_t* p, uint16_t i, quarry_brf_value_t* out) {
    const ChildCtx* c = (const ChildCtx*)p->context; *out = (quarry_brf_value_t){0};
    if (i == 0U && c->child->has_id) { out->kind = QUARRY_BRF_ENCODE_UINT; out->uint_value = c->child->id; return QUARRY_GENERIC_OK; }
    if (i == 1U && c->child->has_label) { out->kind = QUARRY_BRF_ENCODE_STRING; out->string_value = (quarry_string_view_t){c->child->label, c->child->label_length}; return QUARRY_GENERIC_OK; }
    if (i == 2U && c->child->has_payload) { out->kind = QUARRY_BRF_ENCODE_BYTES; out->bytes_value = (quarry_bytes_view_t){c->child->payload, c->child->payload_length}; return QUARRY_GENERIC_OK; }
    return QUARRY_GENERIC_OK;
}
static quarry_generic_status_t uint_element(const quarry_brf_array_provider_t* p, size_t i, quarry_brf_value_t* out) {
    const UintArrayCtx* c = (const UintArrayCtx*)p->context; if (i >= c->count) return QUARRY_GENERIC_FIELD_NOT_FOUND;
    *out = (quarry_brf_value_t){.kind = QUARRY_BRF_ENCODE_UINT, .uint_value = c->values[i]}; return QUARRY_GENERIC_OK;
}
static quarry_generic_status_t child_element(const quarry_brf_record_array_provider_t* p, size_t i, const quarry_brf_record_provider_t** out) {
    const ChildArrayCtx* c = (const ChildArrayCtx*)p->context; static quarry_brf_record_provider_t providers[8]; static ChildCtx contexts[8];
    if (i >= c->count || out == NULL) return QUARRY_GENERIC_FIELD_NOT_FOUND;
    contexts[i].child = &c->items[i]; providers[i] = (quarry_brf_record_provider_t){child_field, &contexts[i]}; *out = &providers[i]; return QUARRY_GENERIC_OK;
}
static quarry_generic_status_t root_field(const quarry_brf_value_provider_t* p, uint16_t i, quarry_brf_value_t* out) {
    const RootCtx* c = (const RootCtx*)p->context; benchmark_workload_Workload_t* r = c->record; static quarry_brf_array_provider_t values; static quarry_brf_record_provider_t child; static ChildCtx child_ctx; static quarry_brf_record_array_provider_t children; static ChildArrayCtx children_ctx; static UintArrayCtx values_ctx;
    *out = (quarry_brf_value_t){0};
    if (i == 0U && r->has_sequence) { out->kind=QUARRY_BRF_ENCODE_UINT; out->uint_value=r->sequence; }
    else if (i == 1U && r->has_timestamp) { out->kind=QUARRY_BRF_ENCODE_UINT; out->uint_value=r->timestamp; }
    else if (i == 2U && r->has_counter) { out->kind=QUARRY_BRF_ENCODE_UINT; out->uint_value=r->counter; }
    else if (i == 3U && r->has_ratio) { out->kind=QUARRY_BRF_ENCODE_FLOAT; out->float_value=r->ratio; }
    else if (i == 4U && r->has_enabled) { out->kind=QUARRY_BRF_ENCODE_BOOL; out->bool_value=r->enabled; }
    else if (i == 5U && r->has_status) { out->kind=QUARRY_BRF_ENCODE_ENUM; out->int_value=r->status; }
    else if (i == 6U && r->has_name) { out->kind=QUARRY_BRF_ENCODE_STRING; out->string_value=(quarry_string_view_t){r->name,r->name_length}; }
    else if (i == 7U && r->has_payload) { out->kind=QUARRY_BRF_ENCODE_BYTES; out->bytes_value=(quarry_bytes_view_t){r->payload,r->payload_length}; }
    else if (i == 8U && r->has_values) { values_ctx=(UintArrayCtx){r->values,r->values_count}; values=(quarry_brf_array_provider_t){uint_element,r->values_count,&values_ctx}; out->kind=QUARRY_BRF_ENCODE_ARRAY; out->aggregate=&values; }
    else if (i == 9U && r->has_child) { child_ctx.child=&r->child; child=(quarry_brf_record_provider_t){child_field,&child_ctx}; out->kind=QUARRY_BRF_ENCODE_RECORD; out->aggregate=&child; }
    else if (i == 10U && r->has_children) { children_ctx=(ChildArrayCtx){r->children,r->children_count}; children=(quarry_brf_record_array_provider_t){child_element,r->children_count,&children_ctx}; out->kind=QUARRY_BRF_ENCODE_ARRAY; out->aggregate=&children; }
    return QUARRY_GENERIC_OK;
}
static int load(const char* path, uint8_t* b, size_t* n) { FILE* f=fopen(path,"rb"); long z; if(!f||fseek(f,0,SEEK_END)|| (z=ftell(f))<0 || (size_t)z>QBS_BYTES || fseek(f,0,SEEK_SET)) return 0; *n=(size_t)z; int ok=fread(b,1,*n,f)==*n; fclose(f); return ok; }
static size_t file_size(const char* path) { FILE* f=fopen(path,"rb"); long z; if(f==NULL||fseek(f,0,SEEK_END)!=0){if(f!=NULL)fclose(f);return 0U;} z=ftell(f); fclose(f); return z<0?0U:(size_t)z; }
static double ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (double)t.tv_sec*1e9+t.tv_nsec; }
static void init_record(benchmark_workload_Workload_t* r) { benchmark_workload_Workload_init(r); r->has_sequence=true;r->sequence=153;r->has_timestamp=true;r->timestamp=1700000000;r->has_counter=true;r->counter=9001;r->has_ratio=true;r->ratio=0.875F;r->has_enabled=true;r->enabled=true;r->has_status=true;r->status=BENCHMARK_WORKLOAD_STATUS_ACTIVE;r->has_name=true;memcpy(r->name,"qbs-workload",12);r->name_length=12;r->has_payload=true;r->payload_length=4;r->payload[0]=1;r->payload[1]=0;r->payload[2]=0xff;r->payload[3]=2;r->has_values=true;r->values_count=4;for(size_t i=0;i<4;i++)r->values[i]=(uint32_t)(i+10);r->has_child=true;r->child.has_id=true;r->child.id=7;r->child.has_label=true;memcpy(r->child.label,"child",5);r->child.label_length=5;r->child.has_payload=true;r->child.payload_length=2;r->child.payload[0]=0;r->child.payload[1]=0xff;r->has_children=true;r->children_count=2;r->children[0]=r->child;r->children[1]=r->child;r->children[1].id=8; }
static int generic_access(const quarry_brf_record_view_t* view, uint64_t* sum) {
    uint64_t value; quarry_string_view_t text; quarry_bytes_view_t bytes;
    quarry_brf_array_view_t array; quarry_brf_record_view_t child, element;
    if (quarry_brf_get_uint(view, 0U, &value) != QUARRY_GENERIC_OK) { fprintf(stderr,"uint\n"); return 0; } *sum += value;
    if (quarry_brf_get_string(view, 6U, &text) != QUARRY_GENERIC_OK) { fprintf(stderr,"string\n"); return 0; } *sum += text.size;
    if (quarry_brf_get_bytes(view, 7U, &bytes) != QUARRY_GENERIC_OK) { fprintf(stderr,"bytes\n"); return 0; } *sum += bytes.size;
    if (quarry_brf_get_array(view, 8U, &array) != QUARRY_GENERIC_OK) { fprintf(stderr,"array\n"); return 0; }
    for (size_t i = 0U; i < array.count; ++i) { if (quarry_brf_array_get_uint(view, &array, i, &value) != QUARRY_GENERIC_OK) { fprintf(stderr,"array uint\n"); return 0; } *sum += value; }
    if (quarry_brf_get_record(view, 9U, &child) != QUARRY_GENERIC_OK) { fprintf(stderr,"record\n"); return 0; }
    if (quarry_brf_get_uint(&child, 0U, &value) != QUARRY_GENERIC_OK) { fprintf(stderr,"child uint\n"); return 0; } *sum += value;
    quarry_brf_decoded_value_t record_array_value;
    if (quarry_brf_get_value(view, 10U, &record_array_value) != QUARRY_GENERIC_OK ||
        record_array_value.kind != QUARRY_BRF_VALUE_ARRAY) { fprintf(stderr,"record array\n"); return 0; }
    array = record_array_value.array;
    for (size_t i = 0U; i < array.count; ++i) { quarry_generic_status_t st = quarry_brf_record_array_get(view, &array, i, &element); if (st != QUARRY_GENERIC_OK) { fprintf(stderr,"array record status=%d index=%zu count=%u type=%d relation=%u cap=%zu elemcap=%zu\n", st, i, array.count, array.element_type, array.relation_index, view->workspace->array_capacity, view->workspace->array_element_capacity); return 0; } if (quarry_brf_get_uint(&element, 0U, &value) != QUARRY_GENERIC_OK) { fprintf(stderr,"array child uint\n"); return 0; } *sum += value; }
    return 1;
}
static int generated_access(const benchmark_workload_Workload_t* r, uint64_t* sum) {
    if (!r->has_sequence || !r->has_name || !r->has_payload || !r->has_values ||
        !r->has_child || !r->has_children || r->children_count != 2U) return 0;
    *sum += r->sequence + r->name_length + r->payload_length;
    for (uint32_t i = 0U; i < r->values_count; ++i) *sum += r->values[i];
    *sum += r->child.id;
    for (uint32_t i = 0U; i < r->children_count; ++i) *sum += r->children[i].id;
    return 1;
}
int main(int argc,char**argv) { (void)argc;(void)argv; uint8_t qbs[QBS_BYTES], generated[BRF_BYTES], generic[BRF_BYTES]; size_t qbs_size=0, generic_size=0;
    quarry_qbs_record_view_t records[8]; quarry_qbs_field_view_t fields[32]; quarry_qbs_type_view_t types[32]; quarry_qbs_enum_view_t enums[4]; uint64_t enum_values[16]; quarry_brf_record_node_t nodes[32]; quarry_brf_field_state_t states[128]; uint32_t maps[128], array_elements[128]; quarry_brf_child_relation_t children[32]; quarry_brf_record_array_relation_t arrays[32]; quarry_brf_validation_frame_t frames[32];
    quarry_workspace_t ws={.records=records,.record_capacity=8,.fields=fields,.field_capacity=32,.types=types,.type_capacity=32,.enums=enums,.enum_capacity=4,.enum_values=enum_values,.enum_value_capacity=16,.nodes=nodes,.node_capacity=32,.field_states=states,.field_state_capacity=128,.field_maps=maps,.field_map_capacity=128,.children=children,.child_capacity=32,.arrays=arrays,.array_capacity=32,.array_elements=array_elements,.array_element_capacity=128,.frames=frames,.frame_capacity=32}; quarry_generic_limits_t lim={QBS_BYTES,BRF_BYTES,128,32,128}; quarry_qbs_view_t schema={0};
    const char* path=QUARRY_BENCHMARK_QBS_PATH; if(!load(path,qbs,&qbs_size)||quarry_qbs_parse(qbs,qbs_size,&schema,&ws,&lim)!=QUARRY_GENERIC_OK) return 1; const quarry_qbs_record_view_t* root=NULL; if(quarry_qbs_find_record_by_name(&schema,"benchmark.workload.Workload",sizeof("benchmark.workload.Workload")-1U,&root)!=QUARRY_GENERIC_OK) return 1; benchmark_workload_Workload_t record; init_record(&record); const RootCtx ctx={&record}; const quarry_brf_value_provider_t provider={root_field,&ctx}; quarry_brf_encoder_field_t ef[16]; quarry_brf_encoder_array_element_t ea[64]; quarry_brf_nested_record_plan_t ep[16]; quarry_brf_nested_frame_t nf[16]; quarry_brf_nested_field_plan_t nfields[64]; quarry_brf_nested_record_array_plan_t na[16]; quarry_brf_record_array_element_plan_t nep[64]; quarry_brf_encoder_workspace_t encoder={0}; encoder.fields=ef; encoder.field_capacity=16; encoder.array_elements=ea; encoder.array_element_capacity=64; encoder.nested.records=ep; encoder.nested.record_capacity=16; encoder.nested.frames=nf; encoder.nested.frame_capacity=16; encoder.nested.fields=nfields; encoder.nested.field_capacity=64; encoder.nested.arrays=na; encoder.nested.array_capacity=16; encoder.nested.array_elements=nep; encoder.nested.array_element_capacity=64; quarry_brf_writer_frame_t wf[16]; quarry_brf_writer_workspace_t writer={wf,16};
    const benchmark_workload_Workload_encode_result_t gr=benchmark_workload_Workload_encode(&record,generated,sizeof(generated)); quarry_generic_status_t gs=quarry_brf_encode(&schema,root,&provider,generic,sizeof(generic),&generic_size,&encoder,&writer,NULL); if(gr.status!=QUARRY_C_STATUS_OK||gs!=QUARRY_GENERIC_OK||generic_size!=gr.bytes_written||memcmp(generic,generated,generic_size)!=0) { size_t d=0; while(d<generic_size&&d<gr.bytes_written&&generic[d]==generated[d]) ++d; fprintf(stderr,"equality failure at byte %zu\n",d); return 2; }
    quarry_brf_record_view_t generic_view; uint64_t generic_sum=0U;
    quarry_generic_status_t validation_status = quarry_brf_validate_with_workspace(
        &schema, root, generated, gr.bytes_written, &generic_view, &ws, &lim);
    if (validation_status != QUARRY_GENERIC_OK) { fprintf(stderr, "validation=%d\n", validation_status); return 3; }
    if (!generic_access(&generic_view, &generic_sum)) { fprintf(stderr, "generic access failed\n"); return 3; }
    const benchmark_workload_Workload_decode_result_t decoded =
        benchmark_workload_Workload_decode(generated, gr.bytes_written);
    if (decoded.status != QUARRY_C_STATUS_OK || !decoded.value.has_sequence) return 3;
    uint64_t generated_sum = 0U;
    if (!generated_access(&decoded.value, &generated_sum) || generated_sum != generic_sum) {
        fprintf(stderr, "checksum mismatch generated=%" PRIu64 " generic=%" PRIu64 "\n", generated_sum, generic_sum);
        return 4;
    }
    enum { RUNS = 3000 };
    double startup=0.0, enc=0.0, gen_enc=0.0, gen_access=0.0, validation=0.0, generic_access_time=0.0;
    for (size_t i=0; i<RUNS; ++i) { quarry_qbs_view_t s={0}; quarry_workspace_reset(&ws); double a=ns(); if(quarry_qbs_parse(qbs,qbs_size,&s,&ws,&lim)!=QUARRY_GENERIC_OK)return 1; startup+=ns()-a; }
    for (size_t i=0; i<RUNS; ++i) { quarry_brf_encoder_workspace_reset(&encoder); double a=ns(); if(quarry_brf_encode(&schema,root,&provider,generic,sizeof(generic),&generic_size,&encoder,&writer,NULL)!=QUARRY_GENERIC_OK)return 1; enc+=ns()-a; }
    for (size_t i=0; i<RUNS; ++i) { double a=ns(); if(benchmark_workload_Workload_encode(&record,generated,sizeof(generated)).status != QUARRY_C_STATUS_OK)return 1; gen_enc+=ns()-a; }
    for (size_t i=0; i<RUNS; ++i) { double a=ns(); if (!generated_access(&decoded.value, &generated_sum)) return 3; gen_access+=ns()-a; }
    for (size_t i=0; i<RUNS; ++i) { double a=ns(); quarry_brf_record_view_t v; if(quarry_brf_validate_with_workspace(&schema,root,generated,gr.bytes_written,&v,&ws,&lim)!=QUARRY_GENERIC_OK)return 3; validation+=ns()-a; }
    for (size_t i=0; i<RUNS; ++i) { double a=ns(); if (!generic_access(&generic_view, &generic_sum)) return 3; generic_access_time+=ns()-a; }
    const double parse_avg=startup/RUNS, encode_avg=enc/RUNS, generated_avg=gen_access/RUNS;
    const double validation_avg=validation/RUNS, generic_avg=generic_access_time/RUNS;
    printf("QBS/generated-C benchmark\nqbs_bytes=%zu generated_header_bytes=%zu generated_source_bytes=%zu generated_total_header_bytes=%zu generated_total_source_bytes=%zu brf_bytes=%zu parser_workspace_configured=%zu parse_ns=%g parse_ops_sec=%g generated_encode_ns=%g generated_encode_ops_sec=%g generic_encode_ns=%g generic_encode_ops_sec=%g validate_ns=%g validate_ops_sec=%g generated_access_ns=%g generated_access_ops_sec=%g generic_access_ns=%g generic_access_ops_sec=%g generated_checksum=%" PRIu64 " generic_checksum=%" PRIu64 " equality=pass\n", qbs_size, file_size(QUARRY_BENCHMARK_GENERATED_HEADER), file_size(QUARRY_BENCHMARK_GENERATED_SOURCE), file_size(QUARRY_BENCHMARK_GENERATED_HEADER)+file_size(QUARRY_BENCHMARK_IMPORTED_HEADER), file_size(QUARRY_BENCHMARK_GENERATED_SOURCE)+file_size(QUARRY_BENCHMARK_IMPORTED_SOURCE), generic_size, sizeof(nodes)+sizeof(states)+sizeof(maps)+sizeof(array_elements)+sizeof(children)+sizeof(arrays)+sizeof(frames), parse_avg, 1e9/parse_avg, gen_enc/RUNS, 1e9/(gen_enc/RUNS), encode_avg, 1e9/encode_avg, validation_avg, 1e9/validation_avg, generated_avg, 1e9/generated_avg, generic_avg, 1e9/generic_avg, generated_sum, generic_sum);
    return 0; }
