#include "quarry/runtime_c/generic_brf.h"

#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr size_t kMaxInput = 64U * 1024U * 1024U;
constexpr size_t kMaxEntries = 1U * 1024U * 1024U;

void usage(FILE* out) {
    std::fprintf(out,
        "usage: quarry-brf-inspect --qbs PATH --brf PATH "
        "(--record-id ID | --record-name NAME) [options]\n\n"
        "Options:\n"
        "  --qbs PATH                 QBS schema image\n"
        "  --brf PATH                 BRF record image, or - for stdin\n"
        "  --record-id ID             Select the record by numeric ID\n"
        "  --record-name NAME         Select the record by reflective name\n"
        "  --list-records             List QBS records and exit (standalone mode)\n"
        "  --indent-width N           Spaces per nesting level (default: 2)\n"
        "  --max-output-bytes N       Output limit (default: unlimited)\n"
        "  -h, --help                 Show this help text\n");
}

bool number(const char* text, size_t* out) {
    if (!text || !*text || text[0] == '-') return false;
    char* end = nullptr; errno = 0;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (errno || end == text || *end || value > SIZE_MAX) return false;
    *out = static_cast<size_t>(value); return true;
}
bool read_input(const std::string& path, std::vector<uint8_t>& out) {
    std::istream* input = nullptr; std::ifstream file;
    if (path == "-") { input = &std::cin; }
    else { file.open(path, std::ios::binary); input = &file; }
    if (!*input) return false;
    if (path == "-") {
        std::vector<char> bytes((std::istreambuf_iterator<char>(*input)), {});
        if (bytes.size() > kMaxInput) return false;
        out.assign(bytes.begin(), bytes.end());
        return !input->bad();
    }
    input->seekg(0, std::ios::end);
    const std::streamoff end = input->tellg();
    if (end < 0 || static_cast<uint64_t>(end) > kMaxInput) return false;
    input->seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(end));
    if (!out.empty()) input->read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<size_t>(input->gcount()) == out.size();
}
int write_stdout(const char* data, size_t size, void*) {
    return std::fwrite(data, 1, size, stdout) == size ? 0 : 1;
}
size_t cap(size_t n, size_t divisor) {
    const size_t value = n / divisor + 1U;
    return value < kMaxEntries ? value : kMaxEntries;
}
void error_status(const char* what, quarry_generic_status_t status) {
    std::fprintf(stderr, "quarry-brf-inspect: %s (status %d)\n", what, static_cast<int>(status));
}
}

int main(int argc, char** argv) {
    std::string qbs_path, brf_path, record_name; size_t record_id = 0, indent = 2, output_limit = SIZE_MAX;
    bool have_id = false, have_name = false, list_records = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        if (a == "--list-records") { if (list_records) { std::fprintf(stderr, "quarry-brf-inspect: duplicate --list-records option\n"); return 2; } list_records = true; continue; }
        auto value = [&](const char* option, std::string& dst) -> bool {
            if (a != option || i + 1 >= argc || !*argv[i + 1]) return false;
            dst = argv[++i]; return true;
        };
        if (value("--qbs", qbs_path) || value("--brf", brf_path) || value("--record-name", record_name)) {
            if (a == "--record-name") have_name = true; continue;
        }
        if (a == "--record-id" && i + 1 < argc && number(argv[i + 1], &record_id)) { ++i; have_id = true; continue; }
        size_t parsed = 0;
        if (a == "--indent-width" && i + 1 < argc && number(argv[i + 1], &parsed)) { indent = parsed; ++i; continue; }
        if (a == "--max-output-bytes" && i + 1 < argc && number(argv[i + 1], &output_limit)) { ++i; continue; }
        std::fprintf(stderr, "quarry-brf-inspect: invalid option or value: %s\n", a.c_str()); usage(stderr); return 2;
    }
    if (list_records && (qbs_path.empty() || !brf_path.empty() || have_id || have_name)) {
        std::fprintf(stderr, "quarry-brf-inspect: --list-records is a standalone QBS discovery mode and cannot be combined with BRF input or record selectors\n");
        return 2;
    }
    if (!list_records && (qbs_path.empty() || brf_path.empty() || have_id == have_name ||
        (have_id && record_id > UINT32_MAX))) {
        std::fprintf(stderr, "quarry-brf-inspect: exactly one record selector is required\n"); usage(stderr); return 2;
    }
    std::vector<uint8_t> qbs_bytes, brf_bytes;
    if (!read_input(qbs_path, qbs_bytes) || (!list_records && !read_input(brf_path, brf_bytes))) {
        std::fprintf(stderr, "quarry-brf-inspect: unable to read input\n"); return 3;
    }
    const size_t rc = cap(qbs_bytes.size(), 29), fc = cap(qbs_bytes.size(), 28), tc = cap(qbs_bytes.size(), 16), ec = cap(qbs_bytes.size(), 16);
    const size_t work = cap(qbs_bytes.size() + brf_bytes.size(), 1);
    std::vector<quarry_qbs_record_view_t> records(rc); std::vector<quarry_qbs_field_view_t> fields(fc);
    std::vector<quarry_qbs_type_view_t> types(tc); std::vector<quarry_qbs_enum_view_t> enums(ec); std::vector<uint64_t> values(ec);
    std::vector<quarry_brf_record_node_t> nodes(work); std::vector<quarry_brf_field_state_t> field_states(work);
    std::vector<uint32_t> maps(work), array_elements(work); std::vector<quarry_brf_child_relation_t> children(work);
    std::vector<quarry_brf_record_array_relation_t> arrays(work); std::vector<quarry_brf_validation_frame_t> frames(work);
    quarry_workspace_t ws{}; ws.records=records.data(); ws.record_capacity=rc; ws.fields=fields.data(); ws.field_capacity=fc; ws.types=types.data(); ws.type_capacity=tc; ws.enums=enums.data(); ws.enum_capacity=ec; ws.enum_values=values.data(); ws.enum_value_capacity=ec; ws.nodes=nodes.data(); ws.node_capacity=work; ws.field_states=field_states.data(); ws.field_state_capacity=work; ws.field_maps=maps.data(); ws.field_map_capacity=work; ws.children=children.data(); ws.child_capacity=work; ws.arrays=arrays.data(); ws.array_capacity=work; ws.array_elements=array_elements.data(); ws.array_element_capacity=work; ws.frames=frames.data(); ws.frame_capacity=work;
    const quarry_generic_limits_t generic_limits{kMaxInput, kMaxInput, work, work, work};
    quarry_qbs_view_t schema{}; auto status = quarry_qbs_parse(qbs_bytes.data(), qbs_bytes.size(), &schema, &ws, &generic_limits);
    if (status != QUARRY_GENERIC_OK) { error_status("QBS parsing failed", status); return 4; }
    if (list_records) {
        for (size_t index = 0U; index < schema.record_count; ++index) {
            const quarry_qbs_record_view_t* listed = &schema.records[index];
            quarry_string_view_t identity{};
            status = quarry_qbs_record_identity(&schema, listed, &identity);
            if (status != QUARRY_GENERIC_OK) { error_status("record identity lookup failed", status); return 4; }
            std::printf("%" PRIu32 " %.*s", listed->record_id, static_cast<int>(identity.size), identity.data);
            quarry_string_view_t display{};
            status = quarry_qbs_record_name(&schema, listed, &display);
            if (status == QUARRY_GENERIC_OK)
                std::printf(" (%.*s)", static_cast<int>(display.size), display.data);
            else if (status != QUARRY_GENERIC_FIELD_ABSENT)
                { error_status("record display-name lookup failed", status); return 4; }
            std::putchar('\n');
        }
        return 0;
    }
    const quarry_qbs_record_view_t* record = nullptr;
    status = have_id ? quarry_qbs_find_record_by_id(&schema, static_cast<uint32_t>(record_id), &record) : quarry_qbs_find_record_by_name(&schema, record_name.c_str(), record_name.size(), &record);
    if (status != QUARRY_GENERIC_OK) { error_status("record selection failed", status); return 5; }
    quarry_brf_record_view_t view{}; status = quarry_brf_validate_with_workspace(&schema, record, brf_bytes.data(), brf_bytes.size(), &view, &ws, &generic_limits);
    if (status != QUARRY_GENERIC_OK) { error_status("BRF validation failed", status); return 6; }
    const size_t depth = work < 4096U ? work : 4096U; std::vector<quarry_brf_traversal_frame_t> traversal(depth); std::vector<quarry_brf_print_frame_t> print(depth);
    quarry_brf_traversal_workspace_t tw{traversal.data(), depth, 0, 0}; quarry_brf_print_workspace_t pw{print.data(), depth, 0, 0};
    const quarry_brf_traversal_limits_t limits{work, depth}; const quarry_brf_print_options_t options{indent, output_limit};
    const auto result = quarry_brf_print(&view, write_stdout, nullptr, &pw, &tw, &limits, &options);
    if (result != QUARRY_BRF_PRINT_COMPLETED) { std::fprintf(stderr, "quarry-brf-inspect: printing failed (status %d)\n", static_cast<int>(result)); return 7; }
    return 0;
}
