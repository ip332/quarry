// Go backend end-to-end tests: generated Go must be gofmt-clean and vet-clean,
// must agree with the C codec on which BRF v2 inputs are valid, and must
// re-encode every valid input to exactly the bytes the C codec produces.
// Skipped when no Go toolchain was found at configure time.

#include "compiler/backend_go/backend_go.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef QUARRY_SCHEMA_COMPILER_TOOL
#error "QUARRY_SCHEMA_COMPILER_TOOL must be defined"
#endif
#ifndef QUARRY_TEST_GO
#define QUARRY_TEST_GO ""
#endif
#ifndef QUARRY_TEST_GOFMT
#define QUARRY_TEST_GOFMT ""
#endif

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory =
        fs::temp_directory_path() /
        (std::string("quarry-go-interop-") + std::string(stem) + "-" + std::to_string(suffix));
    fs::remove_all(directory);
    fs::create_directories(directory);
    return directory;
}

[[nodiscard]] std::string quote(std::string_view value) {
    std::string quoted = "'";
    for (const char c : value) {
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    return quoted + "'";
}

void write_text_file(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error("failed to open " + path.string());
    }
    output << text;
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
    std::ifstream input{path};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] int run(const std::string& command) {
    const int status = std::system(command.c_str());
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + status;
#endif
}

// Hermetic Go environment: no network, no toolchain switching, caches under root.
[[nodiscard]] std::string go_env(const fs::path& root) {
    return "env GOTOOLCHAIN=local GOPROXY=off GOFLAGS=-buildvcs=false GO111MODULE=on GOCACHE=" +
           quote((root / "gocache").string()) + " GOPATH=" + quote((root / "gopath").string()) +
           " ";
}

[[nodiscard]] int compile(const fs::path& schema, const fs::path& output, const std::string& extra,
                          const fs::path& log) {
    return run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " " + extra + " --output-directory " +
               quote(output.string()) + " " + quote(schema.string()) + " > " + quote(log.string()) +
               " 2>&1");
}

constexpr std::string_view kKindsSchema = R"(namespace: opskinds
record: Sample
version: 1
type: data
enums:
  Mode:
    values:
      off: 0
      low: 3
      high: 300
fields:
  flag:
    type: bool
  a:
    type: i8
  b:
    type: u8
  c:
    type: i16
  d:
    type: u16
  e:
    type: i32
  f:
    type: u32
  g:
    type: i64
  h:
    type: u64
  r:
    type: f32
  s:
    type: f64
  mode:
    type: Mode
  text:
    type: string
    max_bytes: 9
  blob:
    type: bytes
    max_bytes: 5
  numbers:
    type: u16[]
    max_elements: 4
  modes:
    type: Mode[]
    max_elements: 3
  flags:
    type: bool[]
    max_elements: 9
)";

constexpr std::string_view kGoMod = R"(module example.com/gen

go 1.22

require github.com/ip332/quarry/runtime/go v0.0.0

replace github.com/ip332/quarry/runtime/go => RUNTIME
)";

class GoInteropTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::string_view(QUARRY_TEST_GO).empty()) {
            GTEST_SKIP() << "Go toolchain not found at configure time";
        }
    }

    // Generates C and Go for the field-kinds schema and the benchmark schemas
    // (which exercise an imported nested record and record arrays) and writes
    // the Go module file.
    void generate(const fs::path& root) {
        const fs::path kinds = root / "kinds.brd";
        write_text_file(kinds, kKindsSchema);
        const fs::path schemas = fs::path(QUARRY_TEST_SOURCE_DIR) / "benchmarks/schemas";
        for (const fs::path& schema :
             {kinds, schemas / "workload_shared.brd", schemas / "workload.brd"}) {
            ASSERT_EQ(compile(schema, root / "c", "--language c", root / "c.log"), 0)
                << read_text_file(root / "c.log");
            ASSERT_EQ(compile(schema, root / "go",
                              "--language go --go-import-prefix example.com/gen", root / "go.log"),
                      0)
                << read_text_file(root / "go.log");
        }
        std::string mod(kGoMod);
        mod.replace(mod.find("RUNTIME"), 7,
                    (fs::path(QUARRY_TEST_SOURCE_DIR) / "runtime/go").string());
        write_text_file(root / "go" / "go.mod", mod);
    }
};

TEST(GoBackendTest, RuntimeEpochMatchesBackendEpoch) {
    const std::string runtime =
        read_text_file(fs::path(QUARRY_TEST_SOURCE_DIR) / "runtime/go/brf/brf.go");
    std::smatch match;
    ASSERT_TRUE(
        std::regex_search(runtime, match, std::regex(R"(const GeneratedCodeAPIVersion = (\d+))")));
    EXPECT_EQ(std::stoul(match[1].str()), quarry::compiler::backend_go::kGeneratedCodeApiVersionGo);
}

TEST_F(GoInteropTest, GeneratedGoIsGofmtCleanAndVetClean) {
    const fs::path root = make_temp_directory("vet");
    generate(root);
    if (HasFatalFailure()) {
        return;
    }
    if (!std::string_view(QUARRY_TEST_GOFMT).empty()) {
        const fs::path unformatted = root / "gofmt.txt";
        ASSERT_EQ(run(quote(QUARRY_TEST_GOFMT) + " -l " + quote((root / "go").string()) + " > " +
                      quote(unformatted.string()) + " 2>&1"),
                  0);
        EXPECT_EQ(read_text_file(unformatted), "") << "files are not gofmt-formatted";
    }
    const fs::path log = root / "vet.log";
    EXPECT_EQ(run("cd " + quote((root / "go").string()) + " && " + go_env(root) +
                  quote(QUARRY_TEST_GO) + " vet ./... > " + quote(log.string()) + " 2>&1"),
              0)
        << read_text_file(log);
}

TEST_F(GoInteropTest, RuntimeUnitTestsPass) {
    const fs::path root = make_temp_directory("runtime");
    const fs::path log = root / "test.log";
    EXPECT_EQ(run("cd " + quote((fs::path(QUARRY_TEST_SOURCE_DIR) / "runtime/go").string()) +
                  " && " + go_env(root) + quote(QUARRY_TEST_GO) + " test ./... > " +
                  quote(log.string()) + " 2>&1"),
              0)
        << read_text_file(log);
}

// The C harness synthesizes valid records from the public C descriptors,
// mutates them, and records for each input whether the C codec accepts it
// and, if so, the C codec's canonical re-encoding. The Go program must agree
// on every accept/reject decision and re-encode every accepted input to the
// same bytes.
constexpr std::string_view kCHarness = R"CPP(
#include "opskinds.generated.h"
#include "benchmark/workload.generated.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
uint32_t state = 88172645U;
uint32_t next() { state ^= state << 13U; state ^= state >> 17U; state ^= state << 5U; return state; }
void fill_record(const quarry_c_type_descriptor_t& type, uint8_t* value);
void fill_element(const quarry_c_element_descriptor_t& e, uint8_t* p) {
    switch (e.kind) {
    case QUARRY_C_TYPED_BOOL: { const bool v = (next() & 1U) != 0U; std::memcpy(p, &v, sizeof(v)); break; }
    case QUARRY_C_TYPED_ENUM: e.enum_store(p, e.enum_values[next() % e.enum_count]); break;
    case QUARRY_C_TYPED_STRING:
    case QUARRY_C_TYPED_BYTES: {
        const uint32_t n = next() % (e.bound + 1U);
        for (uint32_t i = 0U; i < n; ++i) {
            if (e.kind == QUARRY_C_TYPED_STRING) {
                p[i] = static_cast<uint8_t>(n - i >= 2U && next() % 4U == 0U ? 0xC3 : 'a' + next() % 26U);
                if (p[i] == 0xC3) p[++i] = 0xA9; // two-byte UTF-8 sequence
            } else {
                p[i] = static_cast<uint8_t>(next());
            }
        }
        if (e.kind == QUARRY_C_TYPED_STRING) p[n] = 0U;
        std::memcpy(p + e.length_offset, &n, sizeof(n));
        break;
    }
    case QUARRY_C_TYPED_RECORD: fill_record(*e.record, p); break;
    default: for (size_t i = 0U; i < e.size; ++i) p[i] = static_cast<uint8_t>(next());
    }
}
void fill_record(const quarry_c_type_descriptor_t& type, uint8_t* value) {
    for (uint32_t i = 0U; i < type.layout->field_count; ++i) {
        const quarry_c_member_descriptor_t& m = type.members[i];
        if (next() % 3U == 0U) continue;
        if ((m.presence_offset & QUARRY_C_PRESENCE_COUNT_FLAG) != 0U) {
            if (m.max_count == 0U) { fill_element(m.element, value + m.value_offset); continue; }
            const uint32_t count = next() % (m.max_count + 1U);
            for (uint32_t j = 0U; j < count; ++j)
                fill_element(m.element, value + m.value_offset + j * m.element.size);
            std::memcpy(value + m.count_offset, &count, sizeof(count));
            continue;
        }
        if ((m.presence_offset & QUARRY_C_PRESENCE_PACKED_FLAG) != 0U) {
            const size_t bit = m.presence_offset & ~QUARRY_C_PRESENCE_PACKED_FLAG;
            value[bit / 8U] = static_cast<uint8_t>(value[bit / 8U] | (1U << (bit % 8U)));
        } else {
            const bool yes = true;
            std::memcpy(value + m.presence_offset, &yes, sizeof(yes));
        }
        fill_element(m.element, value + m.value_offset);
    }
}
void hex(FILE* out, const uint8_t* p, size_t n) {
    if (n == 0U) { std::fputc('-', out); return; }
    for (size_t i = 0U; i < n; ++i) std::fprintf(out, "%02x", p[i]);
}
int emit(FILE* out, const char* name, const quarry_c_type_descriptor_t& type) {
    const uint32_t id = type.layout->record_id;
    static const uint8_t boundary[] = {0U, 1U, 2U, 0x7FU, 0x80U, 0xC0U, 0xFFU};
    for (int round = 0; round < 80; ++round) {
        std::vector<uint8_t> value(type.size, 0U), wire(1U << 16U);
        fill_record(type, value.data());
        const auto encoded = quarry_c_typed_encode(&type, value.data(), id, wire.data(), wire.size());
        if (encoded.status != QUARRY_C_STATUS_OK) { std::printf("%s: seed encode %u\n", name, encoded.status); return 1; }
        wire.resize(encoded.bytes_written);
        for (int trial = 0; trial < 150; ++trial) {
            std::vector<uint8_t> bytes = wire;
            if (trial != 0) {
                const uint32_t action = next() % 10U;
                if (action == 0U) {
                    bytes.resize(16U + next() % (bytes.size() - 16U)); // truncate
                } else if (action == 1U) {
                    bytes.push_back(static_cast<uint8_t>(next()));     // trailing byte
                } else {
                    for (uint32_t k = 0U, n = 1U + next() % 2U; k < n; ++k)
                        bytes[16U + next() % (bytes.size() - 16U)] =
                            next() % 2U ? boundary[next() % 7U] : static_cast<uint8_t>(next());
                }
            }
            std::vector<uint8_t> decoded(type.size, 0xA5U), canonical(1U << 16U);
            const auto result = quarry_c_typed_decode(&type, bytes.data(), bytes.size(), id, decoded.data());
            size_t canonical_size = 0U;
            if (result.status == QUARRY_C_STATUS_OK) {
                const auto again = quarry_c_typed_encode(&type, decoded.data(), id, canonical.data(), canonical.size());
                if (again.status != QUARRY_C_STATUS_OK) { std::printf("%s: re-encode %u\n", name, again.status); return 1; }
                canonical_size = again.bytes_written;
            }
            std::fprintf(out, "%s ", name);
            hex(out, bytes.data(), bytes.size());
            std::fprintf(out, " %d ", result.status == QUARRY_C_STATUS_OK ? 1 : 0);
            hex(out, canonical.data(), canonical_size);
            std::fputc('\n', out);
        }
    }
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    FILE* out = std::fopen(argv[1], "w");
    if (out == nullptr) return 2;
    int status = emit(out, "Sample", opskinds_Sample_type);
    if (status == 0) status = emit(out, "Child", benchmark_workload_shared_Child_type);
    if (status == 0) status = emit(out, "Workload", benchmark_workload_Workload_type);
    std::fclose(out);
    return status;
}
)CPP";

constexpr std::string_view kGoChecker = R"GO(package main

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"fmt"
	"os"
	"strings"

	"example.com/gen/benchmark/workload"
	"example.com/gen/benchmark/workload/shared"
	"example.com/gen/opskinds"
)

type codec interface {
	UnmarshalBRF([]byte) error
	MarshalBRF() ([]byte, error)
}

func decodeHex(s string) []byte {
	if s == "-" {
		return nil
	}
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

func main() {
	f, err := os.Open(os.Args[1])
	if err != nil {
		panic(err)
	}
	defer f.Close()
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 1<<20), 1<<22)
	accepted := map[string]int{}
	rejected := map[string]int{}
	line := 0
	for scanner.Scan() {
		line++
		parts := strings.Fields(scanner.Text())
		var m codec
		switch parts[0] {
		case "Sample":
			m = &opskinds.Sample{}
		case "Child":
			m = &shared.Child{}
		case "Workload":
			m = &workload.Workload{}
		default:
			panic(parts[0])
		}
		input := decodeHex(parts[1])
		cAccepts := parts[2] == "1"
		err := m.UnmarshalBRF(input)
		if (err == nil) != cAccepts {
			fmt.Printf("line %d %s: C accepts=%v, Go error=%v\ninput %s\n", line, parts[0], cAccepts, err, parts[1])
			os.Exit(1)
		}
		if !cAccepts {
			rejected[parts[0]]++
			continue
		}
		accepted[parts[0]]++
		out, err := m.MarshalBRF()
		if err != nil {
			fmt.Printf("line %d %s: re-encode failed: %v\n", line, parts[0], err)
			os.Exit(1)
		}
		if want := decodeHex(parts[3]); !bytes.Equal(out, want) {
			fmt.Printf("line %d %s: canonical bytes differ\n go % x\n  c % x\n", line, parts[0], out, want)
			os.Exit(1)
		}
	}
	if err := scanner.Err(); err != nil {
		panic(err)
	}
	for _, name := range []string{"Sample", "Child", "Workload"} {
		if accepted[name] == 0 || rejected[name] == 0 {
			fmt.Printf("%s: accepted %d rejected %d; both outcomes must be exercised\n", name, accepted[name], rejected[name])
			os.Exit(1)
		}
		fmt.Printf("%s: %d accepted, %d rejected\n", name, accepted[name], rejected[name])
	}
}
)GO";

TEST_F(GoInteropTest, GoAgreesWithCOnValidityAndCanonicalBytes) {
    const fs::path root = make_temp_directory("differential");
    generate(root);
    if (HasFatalFailure()) {
        return;
    }
    const fs::path harness = root / "c_cases.cpp";
    write_text_file(harness, kCHarness);
    const fs::path c_exe = root / "c_cases";
    std::ostringstream build;
    build << quote(QUARRY_TEST_CXX_COMPILER) << " -std=c++20 -I" << quote((root / "c").string())
          << " -I" << quote(QUARRY_TEST_REPO_INCLUDE_DIR) << " -I"
          << quote(QUARRY_TEST_GENERATED_INCLUDE_DIR) << " " << quote(harness.string()) << " "
          << quote(QUARRY_TEST_RUNTIME_C_LIBRARY) << " -o " << quote(c_exe.string()) << " > "
          << quote((root / "c_build.log").string()) << " 2>&1";
    ASSERT_EQ(run(build.str()), 0) << read_text_file(root / "c_build.log");
    const fs::path cases = root / "cases.txt";
    ASSERT_EQ(run(quote(c_exe.string()) + " " + quote(cases.string())), 0);

    write_text_file(root / "go" / "cmd" / "checker" / "main.go", kGoChecker);
    const fs::path log = root / "checker.log";
    const int status =
        run("cd " + quote((root / "go").string()) + " && " + go_env(root) + quote(QUARRY_TEST_GO) +
            " run ./cmd/checker " + quote(cases.string()) + " > " + quote(log.string()) + " 2>&1");
    EXPECT_EQ(status, 0) << read_text_file(log);
}

constexpr std::string_view kGoApiTest = R"GO(package apitest

import (
	"bytes"
	"errors"
	"strings"
	"testing"

	"example.com/gen/benchmark/workload"
	"example.com/gen/benchmark/workload/shared"
	"example.com/gen/opskinds"
	"github.com/ip332/quarry/runtime/go/brf"
)

func TestPresenceDistinguishesZeroFromAbsent(t *testing.T) {
	in := opskinds.Sample{F: brf.Ptr(uint32(0)), Flag: brf.Ptr(false)}
	data, err := in.MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	var out opskinds.Sample
	if err := out.UnmarshalBRF(data); err != nil {
		t.Fatal(err)
	}
	if out.F == nil || *out.F != 0 || out.Flag == nil || *out.Flag || out.A != nil {
		t.Fatalf("presence not preserved: %+v", out)
	}
}

func TestEmptyStringsBytesAndArraysEncodeAsAbsent(t *testing.T) {
	empty := opskinds.Sample{Text: "", Blob: []byte{}, Numbers: []uint16{}}
	a, err := empty.MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	b, err := (&opskinds.Sample{}).MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(a, b) {
		t.Fatal("empty values must encode exactly like absent ones")
	}
}

func TestRoundTripAllKinds(t *testing.T) {
	in := opskinds.Sample{
		Flag: brf.Ptr(true), A: brf.Ptr(int8(-128)), B: brf.Ptr(uint8(255)),
		C: brf.Ptr(int16(-2)), D: brf.Ptr(uint16(65535)), E: brf.Ptr(int32(-7)),
		F: brf.Ptr(uint32(1 << 31)), G: brf.Ptr(int64(-1)), H: brf.Ptr(uint64(1<<64 - 1)),
		R: brf.Ptr(float32(-1.5)), S: brf.Ptr(2.25), Mode: brf.Ptr(opskinds.ModeHigh),
		Text: "héllo", Blob: []byte{0, 255}, Numbers: []uint16{1, 65535},
		Modes: []opskinds.Mode{opskinds.ModeOff, opskinds.ModeLow}, Flags: []bool{true, false},
	}
	data, err := in.MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	var out opskinds.Sample
	if err := out.UnmarshalBRF(data); err != nil {
		t.Fatal(err)
	}
	again, err := out.MarshalBRF()
	if err != nil || !bytes.Equal(again, data) {
		t.Fatalf("round trip changed bytes: %v", err)
	}
	if *out.H != 1<<64-1 || *out.Mode != opskinds.ModeHigh || out.Text != "héllo" ||
		out.Modes[1] != opskinds.ModeLow || !out.Flags[0] || out.Flags[1] {
		t.Fatalf("values changed: %+v", out)
	}
}

func TestEncodeValidation(t *testing.T) {
	cases := map[string]opskinds.Sample{
		"mode":    {Mode: brf.Ptr(opskinds.Mode(7))},
		"text":    {Text: "0123456789"},
		"blob":    {Blob: make([]byte, 6)},
		"numbers": {Numbers: make([]uint16, 5)},
		"modes":   {Modes: []opskinds.Mode{opskinds.ModeOff, 9}},
	}
	for field, value := range cases {
		_, err := value.MarshalBRF()
		var fe *brf.FieldError
		var ee *brf.EncodeError
		if !errors.As(err, &fe) || !strings.HasPrefix(fe.Path, field) || !errors.As(err, &ee) {
			t.Errorf("%s: expected an EncodeError for the field, got %v", field, err)
		}
	}
	if _, err := (&opskinds.Sample{Text: "\xff"}).MarshalBRF(); err == nil {
		t.Error("invalid UTF-8 accepted")
	}
}

func TestNestedRecordsUseTheContainingSchemaRecordID(t *testing.T) {
	child := shared.Child{Id: brf.Ptr(uint32(5)), Label: "x"}
	in := workload.Workload{Child: &child, Children: []shared.Child{child, {}}}
	data, err := in.MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	var out workload.Workload
	if err := out.UnmarshalBRF(data); err != nil {
		t.Fatal(err)
	}
	if *out.Child.Id != 5 || out.Child.Label != "x" || len(out.Children) != 2 {
		t.Fatalf("nested values: %+v", out)
	}
	// A standalone Child carries its own schema's ID; the nested one does not.
	alone, _ := child.MarshalBRF()
	if id, _ := brf.PeekRecordID(alone); id != shared.ChildRecordID {
		t.Fatalf("standalone record id %d", id)
	}
	if err := out.UnmarshalBRFWithRecordID(data, workload.WorkloadRecordID+1); err == nil {
		t.Fatal("accepted the wrong record id")
	}
}

func TestDecodeErrorsCarryFieldPaths(t *testing.T) {
	in := workload.Workload{Children: []shared.Child{{}, {Label: "ok"}}}
	data, err := in.MarshalBRF()
	if err != nil {
		t.Fatal(err)
	}
	i := bytes.Index(data, []byte("ok"))
	data[i] = 0xFF // invalid UTF-8 inside children[1].label
	var out workload.Workload
	err = out.UnmarshalBRF(data)
	var de *brf.DecodeError
	if err == nil || !strings.HasPrefix(err.Error(), "children[1].label: ") || !errors.As(err, &de) {
		t.Fatalf("error path: %v", err)
	}
}
)GO";

TEST_F(GoInteropTest, GeneratedGoApiBehaves) {
    const fs::path root = make_temp_directory("api");
    generate(root);
    if (HasFatalFailure()) {
        return;
    }
    write_text_file(root / "go" / "apitest" / "api_test.go", kGoApiTest);
    const fs::path log = root / "api.log";
    EXPECT_EQ(run("cd " + quote((root / "go").string()) + " && " + go_env(root) +
                  quote(QUARRY_TEST_GO) + " test ./apitest > " + quote(log.string()) + " 2>&1"),
              0)
        << read_text_file(log);
}

TEST(GoBackendCliTest, DiagnosticsForGoOptionsAndNames) {
    const fs::path root = make_temp_directory("cli");
    const fs::path schemas = fs::path(QUARRY_TEST_SOURCE_DIR) / "benchmarks/schemas";
    const fs::path log = root / "log.txt";
    // Cross-namespace references need the output directory's import path.
    EXPECT_EQ(compile(schemas / "workload.brd", root / "out", "--language go", log), 1);
    EXPECT_NE(read_text_file(log).find("--go-import-prefix"), std::string::npos)
        << read_text_file(log);
    // The prefix only applies to Go.
    EXPECT_EQ(compile(schemas / "workload_shared.brd", root / "out",
                      "--language c --go-import-prefix example.com/x", log),
              2);
    // Two fields mapping to the same Go identifier are ambiguous.
    const fs::path collision = root / "collision.brd";
    write_text_file(collision, "namespace: dup\nrecord: R\nversion: 1\ntype: data\nfields:\n"
                               "  foo_bar:\n    type: u8\n  fooBar:\n    type: u8\n");
    EXPECT_EQ(compile(collision, root / "out", "--language go", log), 1);
    EXPECT_NE(read_text_file(log).find("ambiguous"), std::string::npos) << read_text_file(log);
    // Keyword namespace segments still produce valid package names; a field
    // named like a generated method is renamed.
    const fs::path keyword = root / "keyword.brd";
    write_text_file(keyword, "namespace: type.go\nrecord: R\nversion: 1\ntype: data\nfields:\n"
                             "  marshal_b_r_f:\n    type: u8\n  count:\n    type: u8\n");
    ASSERT_EQ(compile(keyword, root / "kw", "--language go", log), 0) << read_text_file(log);
    const std::string generated = read_text_file(root / "kw" / "type" / "go" / "schema.quarry.go");
    EXPECT_NE(generated.find("package go_\n"), std::string::npos);
    EXPECT_NE(generated.find("\tMarshalBRF_ *uint8\n"), std::string::npos) << generated;
    // --list-outputs reports the planned file without writing it.
    const fs::path listed = root / "listed.txt";
    ASSERT_EQ(run(quote(QUARRY_SCHEMA_COMPILER_TOOL) + " --language go --list-outputs " +
                  "--output-directory " + quote((root / "planned").string()) + " " +
                  quote((schemas / "workload_shared.brd").string()) + " > " +
                  quote(listed.string())),
              0);
    EXPECT_EQ(read_text_file(listed),
              (root / "planned").string() + "/benchmark/workload/shared/schema.quarry.go\n");
    EXPECT_FALSE(fs::exists(root / "planned"));
}

} // namespace
