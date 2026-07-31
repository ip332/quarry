#include "compiler/backend_python/backend_python.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quarry::compiler::backend_python {

namespace {

using ::quarry::schema_ir::FieldIR;
using ::quarry::schema_ir::FieldType;
using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::PrimitiveType;
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

// --- Scalar field lowering --------------------------------------------
//
// PR-119 scope: bool and every fixed-width signed/unsigned integer and
// f32/f64 scalar primitive -- the same eleven types
// compiler/backend_c/backend_c.cpp's lower_scalar_field_type supports,
// independently re-derived here (not shared, per this backend's own
// established convention of not depending on backend_c). `runtime_type_name`
// is the string literal passed to quarry.runtime.python.binary_record's
// pack_scalar()/unpack_scalar() (e.g. "uint32"); `python_type_hint` is the
// dataclass field's type annotation ("bool", "int", or "float"). No enum,
// string, bytes, array, or nested-record field type is supported yet --
// lower_field_encoding below fails generation with a diagnostic for any of
// those, naming the record and field.

struct ScalarEncoding {
    std::string runtime_type_name;
    std::string python_type_hint;
};

[[nodiscard]] std::optional<ScalarEncoding> lower_scalar_field_type(const FieldType& type) {
    if (type.kind_case() != FieldType::kPrimitive) {
        return std::nullopt;
    }
    switch (type.primitive()) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
        return ScalarEncoding{"bool", "bool"};
    case PrimitiveType::PRIMITIVE_TYPE_I8:
        return ScalarEncoding{"int8", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return ScalarEncoding{"uint8", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_I16:
        return ScalarEncoding{"int16", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return ScalarEncoding{"uint16", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_I32:
        return ScalarEncoding{"int32", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_U32:
        return ScalarEncoding{"uint32", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_I64:
        return ScalarEncoding{"int64", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_U64:
        return ScalarEncoding{"uint64", "int"};
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return ScalarEncoding{"float32", "float"};
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return ScalarEncoding{"float64", "float"};
    case PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED:
    default:
        return std::nullopt;
    }
}

// --- Planning -------------------------------------------------------------

struct PlannedField {
    std::string name;
    std::uint32_t field_index = 0U;
    std::string runtime_type_name;
    std::string python_type_hint;
};

struct PlannedRecord {
    std::string name;
    std::uint32_t record_id = 0U;
    std::vector<PlannedField> fields;
};

struct PlannedNamespaceFile {
    std::string relative_module_path;
    std::vector<PlannedRecord> records;
};

// A namespace emits a module if it directly owns one or more records.
// Enums are deliberately ignored here -- PR-119's scope is scalar record
// fields only; enum rendering is deferred to a later PR, per
// docs/design/python-backend.md's roadmap.
[[nodiscard]] bool namespace_emits_file(const NamespaceIR& ns) { return ns.records_size() > 0; }

// Collects one PlannedNamespaceFile per namespace that directly owns
// records, in Schema IR declaration order (pre-order namespace traversal),
// and accumulates every such namespace's ancestor __init__.py paths into
// `ancestor_init_paths`. Fails with a diagnostic naming the record and
// field for any field whose type is not one of the eleven supported
// scalar primitives.
[[nodiscard]] bool collect_namespace_files(const NamespaceIR& ns, const CodegenOptions& options,
                                          std::vector<PlannedNamespaceFile>& files,
                                          std::set<std::string>& ancestor_init_paths,
                                          std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
        file.relative_module_path = module_path_for_namespace(options, ns.fqn());

        for (const RecordIR& record_ir : ns.records()) {
            std::vector<PlannedField> planned_fields;
            planned_fields.reserve(static_cast<std::size_t>(record_ir.fields_size()));
            for (const FieldIR& field_ir : record_ir.fields()) {
                std::optional<ScalarEncoding> encoding = lower_scalar_field_type(field_ir.type());
                if (!encoding.has_value()) {
                    std::ostringstream stream;
                    stream << "backend_python: field '" << record_ir.fqn() << "."
                           << field_ir.name()
                           << "' has a type the Python backend does not support yet -- only "
                              "bool, fixed-width signed/unsigned integer, and f32/f64 scalar "
                              "fields are supported (see docs/design/python-backend.md); enum, "
                              "string, bytes, array, and nested record fields remain "
                              "unsupported";
                    error_message = stream.str();
                    return false;
                }
                planned_fields.push_back(PlannedField{
                    .name = field_ir.name(),
                    .field_index = field_ir.field_index(),
                    .runtime_type_name = encoding->runtime_type_name,
                    .python_type_hint = encoding->python_type_hint,
                });
            }
            file.records.push_back(PlannedRecord{
                .name = record_ir.name(),
                .record_id = record_ir.record_id(),
                .fields = std::move(planned_fields),
            });
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

// Renders one record: a @dataclass (one Optional[<hint>] = None field per
// declared scalar field, matching PR-117's decided absent/present-via-None
// representation) whose encode/decode/encoded_size methods delegate their
// single line of implementation to the module-level helper functions below
// them (PR-118A's recommended design), plus those helper functions
// themselves, which now perform real BRF scalar encode/decode via
// quarry.runtime.python.binary_record (aliased _brf in the module preamble)
// rather than raising NotImplementedError -- PR-119's scalar milestone.
// _encoded_size_<name> is implemented as len(_encode_<name>(value)): always
// exactly correct by construction, at the cost of doing a full encode to
// learn a size (unlike the C/C++ backends' size-only computation) -- an
// acceptable simplicity/performance tradeoff for this first functional
// milestone, revisitable later without changing the public API.
[[nodiscard]] std::string render_record_block(const PlannedRecord& record) {
    const std::string snake_name = snake_case_from_pascal(record.name);
    std::ostringstream stream;
    stream << "@dataclass\n";
    stream << "class " << record.name << ":\n";
    for (const PlannedField& field : record.fields) {
        stream << "    " << field.name << ": Optional[" << field.python_type_hint
               << "] = None\n";
    }
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
    stream << "    fields = []\n";
    for (const PlannedField& field : record.fields) {
        stream << "    if value." << field.name << " is not None:\n";
        stream << "        fields.append((" << field.field_index << ", _brf.pack_scalar(\""
               << field.runtime_type_name << "\", value." << field.name << ")))\n";
    }
    stream << "    return _brf.encode_record(" << record.record_id << ", fields)\n";
    stream << "\n";
    stream << "\n";

    stream << "def _decode_" << snake_name << "(data):\n";
    stream << "    record_id, fields = _brf.parse_record(data)\n";
    stream << "    if record_id != " << record.record_id << ":\n";
    stream << "        raise _brf.DecodeError(\n";
    stream << "            f\"unexpected record id: {record_id} (expected " << record.record_id
           << ")\")\n";
    for (const PlannedField& field : record.fields) {
        stream << "    " << field.name << " = None\n";
        stream << "    if " << field.field_index << " in fields:\n";
        stream << "        " << field.name << " = _brf.unpack_scalar(\""
               << field.runtime_type_name << "\", fields[" << field.field_index << "])\n";
    }
    stream << "    return " << record.name << "(";
    for (std::size_t index = 0; index < record.fields.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << record.fields[index].name << "=" << record.fields[index].name;
    }
    stream << ")\n";
    stream << "\n";
    stream << "\n";

    stream << "def _encoded_size_" << snake_name << "(value):\n";
    stream << "    return len(_encode_" << snake_name << "(value))\n";
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
    stream << "from quarry.runtime.python import binary_record as _brf\n";
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
    stream << "from typing import Optional\n";

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
