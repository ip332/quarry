#include "compiler/backend_python/backend_python.hpp"

#include <cctype>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quarry::compiler::backend_python {

namespace {

using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::RecordIR;

// Compatibility epoch embedded in every generated module's import-time check
// (see render_module below). Independent from
// QUARRY_GENERATED_CODE_API_VERSION and QUARRY_GENERATED_CODE_API_VERSION_C
// (docs/design/python-backend.md): the Python generator/runtime contract may
// change on its own schedule. Not yet CMake-driven (unlike the C/C++
// epochs): runtime/python/ is a plain pip-installable package outside the
// CMake configure graph, so for now this constant and
// runtime/python's own QUARRY_GENERATED_CODE_API_VERSION_PYTHON literal must
// be kept in sync by hand. A follow-up PR could wire a shared CMake scalar
// through configure_file if that manual step becomes a real pain point.
constexpr std::uint32_t kGeneratedCodeApiVersionPython = 1U;

// --- Namespace naming -------------------------------------------------
//
// Independently derived for the Python backend; intentionally not shared
// with compiler/backend/backend.cpp or compiler/backend_c/backend_c.cpp
// (see docs/design/python-backend.md). Unlike C's flat "last segment
// doubles as the file name" convention, Python namespaces need *real*
// nested packages: every FQN segment becomes its own directory with its
// own __init__.py, and the namespace's own generated module lives one
// level further in, inside its own directory (e.g. FQN "acme.telemetry"
// -> "acme/__init__.py", "acme/telemetry/__init__.py",
// "acme/telemetry/schema.py") -- this is the one place the Python
// backend's file-planning model is structurally different from either
// existing backend's, because only Python's import system requires real
// directories/__init__.py files to establish package identity.

[[nodiscard]] std::vector<std::string> namespace_parts(std::string_view fqn) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : fqn) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

[[nodiscard]] std::string join_with(const std::vector<std::string>& parts, char separator) {
    std::string joined;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            joined.push_back(separator);
        }
        joined.append(parts[index]);
    }
    return joined;
}

// Namespace FQN -> generated module path, e.g. "acme.telemetry" ->
// "acme/telemetry/schema.py". The root (synthetic, unnamed) namespace's
// module lives directly under the output directory with no wrapping
// package, mirroring the root-namespace precedent already established by
// both existing backends' file_stem_for_namespace equivalents.
[[nodiscard]] std::string module_path_for_namespace(const CodegenOptions& options,
                                                    std::string_view fqn) {
    const std::vector<std::string> parts = namespace_parts(fqn);
    if (parts.empty()) {
        return options.root_module_stem + ".py";
    }
    return join_with(parts, '/') + "/" + options.root_module_stem + ".py";
}

// Every ancestor directory of a record-owning namespace needs its own
// __init__.py (e.g. FQN "acme.telemetry" contributes both
// "acme/__init__.py" and "acme/telemetry/__init__.py"). Sibling/cousin
// namespaces sharing a common ancestor (e.g. "acme.telemetry" and
// "acme.control") legitimately contribute the *same* ancestor path more
// than once -- callers collect these into a std::set so that expected
// overlap is deduplicated silently, unlike a genuine module-path
// collision (see build_generation_plan's separate, non-deduplicating
// check for that).
void collect_ancestor_init_paths(std::string_view fqn, std::set<std::string>& init_paths) {
    const std::vector<std::string> parts = namespace_parts(fqn);
    std::string prefix;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            prefix.push_back('/');
        }
        prefix += parts[index];
        init_paths.insert(prefix + "/__init__.py");
    }
}

// PascalCase -> snake_case for generated helper-function names (e.g.
// "Sample" -> "sample", "SensorReading" -> "sensor_reading"). Inserts an
// underscore before each uppercase letter that immediately follows a
// lowercase letter or digit, then lowercases the whole string. This is a
// deliberately simple heuristic that does not special-case acronym runs
// (e.g. "HTTPResponse" -> "h_t_t_p_response", not "http_response"); see
// docs/design/python-backend.md's "Known limitations" section.
[[nodiscard]] std::string snake_case_from_pascal(std::string_view name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        const bool is_upper = std::isupper(static_cast<unsigned char>(ch)) != 0;
        if (is_upper && index > 0) {
            const char previous = name[index - 1];
            const bool previous_is_lower_or_digit =
                std::islower(static_cast<unsigned char>(previous)) != 0 ||
                std::isdigit(static_cast<unsigned char>(previous)) != 0;
            if (previous_is_lower_or_digit) {
                result.push_back('_');
            }
        }
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

// --- Planning -------------------------------------------------------------

struct PlannedRecord {
    std::string name;
};

struct PlannedNamespaceFile {
    std::string relative_module_path;
    std::vector<PlannedRecord> records;
};

// A namespace emits a module in this skeleton only if it directly owns one
// or more records. Enums are deliberately ignored here -- PR-118's scope is
// records only (the given per-record template is the entire generated-code
// surface this PR defines); enum rendering is deferred to a later PR, per
// docs/design/python-backend.md's roadmap.
[[nodiscard]] bool namespace_emits_file(const NamespaceIR& ns) { return ns.records_size() > 0; }

// Collects one PlannedNamespaceFile per namespace that directly owns
// records, in Schema IR declaration order (pre-order namespace traversal),
// and accumulates every such namespace's ancestor __init__.py paths into
// `ancestor_init_paths`. Fails with a diagnostic naming the record and its
// field count for any record declaring one or more fields -- this skeleton
// supports zero-field records only (mirrors PR-107's identical rule for the
// original C backend skeleton).
[[nodiscard]] bool collect_namespace_files(const NamespaceIR& ns, const CodegenOptions& options,
                                          std::vector<PlannedNamespaceFile>& files,
                                          std::set<std::string>& ancestor_init_paths,
                                          std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
        file.relative_module_path = module_path_for_namespace(options, ns.fqn());

        for (const RecordIR& record_ir : ns.records()) {
            if (record_ir.fields_size() > 0) {
                std::ostringstream stream;
                stream << "backend_python: record '" << record_ir.fqn() << "' declares "
                       << record_ir.fields_size()
                       << " field(s); the Python backend skeleton does not yet support any "
                          "field types -- only zero-field records can be generated (see "
                          "docs/design/python-backend.md)";
                error_message = stream.str();
                return false;
            }
            file.records.push_back(PlannedRecord{.name = record_ir.name()});
        }

        collect_ancestor_init_paths(ns.fqn(), ancestor_init_paths);
        files.push_back(std::move(file));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_namespace_files(child, options, files, ancestor_init_paths, error_message)) {
            return false;
        }
    }
    return true;
}

// Single source of truth for file planning, shared by plan() and generate()
// so the two modes cannot diverge -- the same discipline
// compiler/backend/backend.cpp and compiler/backend_c/backend_c.cpp already
// document for C++ and C respectively.
[[nodiscard]] bool build_generation_plan(const schema_ir::SchemaIrModel& schema_ir,
                                        const CodegenOptions& options,
                                        std::vector<std::string>& init_file_paths,
                                        std::vector<PlannedNamespaceFile>& module_files,
                                        std::string& error_message) {
    std::set<std::string> ancestor_init_paths;
    if (!collect_namespace_files(schema_ir.root_namespace(), options, module_files,
                                 ancestor_init_paths, error_message)) {
        return false;
    }

    // Module paths are unique by construction (one per distinct namespace
    // FQN, and FQNs are unique within Schema IR), but this check is kept as
    // a defensive guard against future refactors, mirroring backend_c's own
    // duplicate-output-path check -- unlike ancestor __init__.py paths,
    // which are expected to repeat across sibling namespaces, two different
    // namespaces producing the same module path would be a genuine error.
    std::set<std::string> seen_module_paths;
    for (const PlannedNamespaceFile& file : module_files) {
        if (!seen_module_paths.insert(file.relative_module_path).second) {
            error_message =
                "backend_python: duplicate generated module path: " + file.relative_module_path;
            return false;
        }
    }

    init_file_paths.assign(ancestor_init_paths.begin(), ancestor_init_paths.end());
    return true;
}

// --- Rendering --------------------------------------------------------

[[nodiscard]] std::string render_init_file() {
    return "\"\"\"Generated by Quarry (Python backend). Do not edit by hand.\"\"\"\n";
}

// Reproduces the record template exactly as specified for PR-118: the
// dataclass's methods are the public API and delegate their single line of
// implementation to the module-level helper functions below them (PR-118A's
// recommended design, now wired end to end); the helpers themselves still
// raise NotImplementedError, since no real codec logic exists yet -- there
// is deliberately only one place (the helper) that can raise, not two
// independent copies of the same "not implemented" behavior.
[[nodiscard]] std::string render_record_block(const PlannedRecord& record) {
    const std::string snake_name = snake_case_from_pascal(record.name);
    std::ostringstream stream;
    stream << "@dataclass\n";
    stream << "class " << record.name << ":\n";
    stream << "\n";
    stream << "    def encode(self):\n";
    stream << "        return _encode_" << snake_name << "(self)\n";
    stream << "\n";
    stream << "    @classmethod\n";
    stream << "    def decode(cls, data):\n";
    stream << "        return _decode_" << snake_name << "(data)\n";
    stream << "\n";
    stream << "    def encoded_size(self):\n";
    stream << "        return _encoded_size_" << snake_name << "(self)\n";
    stream << "\n";
    stream << "\n";
    stream << "def _encode_" << snake_name << "(value):\n";
    stream << "    raise NotImplementedError\n";
    stream << "\n";
    stream << "\n";
    stream << "def _decode_" << snake_name << "(data):\n";
    stream << "    raise NotImplementedError\n";
    stream << "\n";
    stream << "\n";
    stream << "def _encoded_size_" << snake_name << "(value):\n";
    stream << "    raise NotImplementedError\n";
    return stream.str();
}

// A namespace's generated module: a file-level epoch-check preamble
// (imports the runtime's compatibility constant and raises ImportError on
// mismatch, mirroring the philosophy -- not the literal compile-time
// mechanism -- of the C/C++ generated-code epoch guards) followed by one
// record block per zero-field record declared in this namespace.
[[nodiscard]] std::string render_module(const PlannedNamespaceFile& file) {
    std::ostringstream stream;
    stream << "\"\"\"Generated by Quarry (Python backend). Do not edit by hand.\"\"\"\n";
    stream << "\n";
    stream << "from quarry.runtime.python import QUARRY_GENERATED_CODE_API_VERSION_PYTHON\n";
    stream << "\n";
    stream << "if QUARRY_GENERATED_CODE_API_VERSION_PYTHON != " << kGeneratedCodeApiVersionPython
           << ":\n";
    stream << "    raise ImportError(\n";
    stream << "        \"Generated Quarry Python code is incompatible with the installed \"\n";
    stream << "        \"Quarry Python runtime. Regenerate the code using a compatible \"\n";
    stream << "        \"quarry-schema-compiler release.\"\n";
    stream << "    )\n";
    stream << "\n";
    stream << "from dataclasses import dataclass\n";

    bool first_record = true;
    for (const PlannedRecord& record : file.records) {
        stream << "\n";
        if (!first_record) {
            stream << "\n";
        }
        stream << render_record_block(record);
        first_record = false;
    }

    return stream.str();
}

} // namespace

std::string output_path_for_planned_file(const CodegenOptions& options,
                                        std::string_view relative_path) {
    if (options.output_directory.empty()) {
        return std::string(relative_path);
    }
    return options.output_directory + "/" + std::string(relative_path);
}

PlanResult Backend::plan(const schema_ir::SchemaIrModel& schema_ir,
                        const CodegenOptions& options) const {
    PlanResult result;

    std::vector<std::string> init_file_paths;
    std::vector<PlannedNamespaceFile> module_files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, init_file_paths, module_files,
                              error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.plan.files.reserve(init_file_paths.size() + module_files.size());
    for (const std::string& path : init_file_paths) {
        result.plan.files.push_back(PlannedGeneratedFile{.relative_output_path = path});
    }
    for (const PlannedNamespaceFile& file : module_files) {
        result.plan.files.push_back(
            PlannedGeneratedFile{.relative_output_path = file.relative_module_path});
    }
    return result;
}

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                               const CodegenOptions& options) const {
    CodegenResult result;

    std::vector<std::string> init_file_paths;
    std::vector<PlannedNamespaceFile> module_files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, init_file_paths, module_files,
                              error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.files.reserve(init_file_paths.size() + module_files.size());
    for (const std::string& path : init_file_paths) {
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, path),
            .content = render_init_file(),
        });
    }
    for (const PlannedNamespaceFile& file : module_files) {
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, file.relative_module_path),
            .content = render_module(file),
        });
    }
    return result;
}

} // namespace quarry::compiler::backend_python
