#include "compiler/backend_python/backend_python.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quarry::compiler::backend_python {

namespace {

using ::quarry::schema_ir::ArrayType;
using ::quarry::schema_ir::EnumIR;
using ::quarry::schema_ir::EnumValueIR;
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

using PythonNameSet = std::set<std::string>;

[[nodiscard]] bool is_python_keyword(std::string_view name) {
    static constexpr std::string_view keywords[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await", "break",
        "case", "class", "continue", "def", "del", "elif", "else", "except", "finally",
        "for", "from", "global", "if", "import", "in", "is", "lambda", "match", "nonlocal",
        "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
    };
    for (const std::string_view keyword : keywords) {
        if (keyword == name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_python_reserved_convention(std::string_view name) {
    if (name.size() >= 4U && name.starts_with("__") && name.ends_with("__")) {
        return true;
    }
    return name.size() >= 3U && name.front() == '_' && name.back() == '_' &&
           name[1] != '_' && name[name.size() - 2U] != '_';
}

[[nodiscard]] std::string sanitize_python_identifier(std::string_view original) {
    std::string result;
    result.reserve(original.size() + 1U);
    for (const unsigned char character : original) {
        const bool ascii_letter = (character >= static_cast<unsigned char>('a') &&
                                   character <= static_cast<unsigned char>('z')) ||
                                  (character >= static_cast<unsigned char>('A') &&
                                   character <= static_cast<unsigned char>('Z'));
        if (ascii_letter || character == '_' ||
            (character >= static_cast<unsigned char>('0') &&
             character <= static_cast<unsigned char>('9') && !result.empty())) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        result = "_";
    } else if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    if (is_python_keyword(result)) {
        result.push_back('_');
    }
    return result;
}

[[nodiscard]] std::optional<std::string> python_name_for(
    std::string_view original, std::string_view context, const PythonNameSet& reserved,
    std::string& error_message) {
    std::string generated = sanitize_python_identifier(original);
    if (generated.starts_with("_quarry_")) {
        error_message = "backend_python: " + std::string(context) + " '" + std::string(original) +
                        "' normalizes to generator-reserved identifier '" + generated + "'";
        return std::nullopt;
    }
    if (is_python_reserved_convention(generated)) {
        error_message = "backend_python: " + std::string(context) + " '" + std::string(original) +
                        "' normalizes to reserved Python identifier '" + generated + "'";
        return std::nullopt;
    }
    if (reserved.contains(generated)) {
        generated.push_back('_');
    }
    return generated;
}

[[nodiscard]] bool insert_python_name(PythonNameSet& names, std::string_view generated,
                                      std::string_view original, std::string_view context,
                                      std::string& error_message) {
    if (names.insert(std::string(generated)).second) {
        return true;
    }
    error_message = "backend_python: " + std::string(context) + " names '" +
                    std::string(original) + "' and another schema identifier both normalize to '" +
                    std::string(generated) + "'; generation is ambiguous";
    return false;
}

[[nodiscard]] const PythonNameSet& module_reserved_names() {
    static const PythonNameSet names = {
        "IntEnum", "Optional", "_brf", "QUARRY_GENERATED_CODE_API_VERSION_PYTHON", "dataclass",
    };
    return names;
}

[[nodiscard]] const PythonNameSet& field_reserved_names() {
    static const PythonNameSet names = {
        "IntEnum", "Optional", "_brf", "QUARRY_GENERATED_CODE_API_VERSION_PYTHON", "dataclass",
        "encode", "decode", "encoded_size", "fields",
        "_array_data", "_count", "_offset", "_index", "_length_offset", "_element_length",
        "_error", "_element_end", "_items", "_array_payload", "_item", "_encoded_item",
    };
    return names;
}

[[nodiscard]] const PythonNameSet& enum_member_reserved_names() {
    static const PythonNameSet names = {"name", "value"};
    return names;
}

[[nodiscard]] const PythonNameSet& namespace_reserved_names() {
    static const PythonNameSet names = {"quarry"};
    return names;
}

[[nodiscard]] std::optional<std::vector<std::string>> sanitize_namespace_parts(
    std::string_view fqn, std::string& error_message) {
    std::vector<std::string> result;
    for (const std::string& original : namespace_parts(fqn)) {
        const std::optional<std::string> generated =
            python_name_for(original, "namespace segment", namespace_reserved_names(), error_message);
        if (!generated.has_value()) {
            return std::nullopt;
        }
        result.push_back(*generated);
    }
    return result;
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
[[nodiscard]] std::optional<std::string> module_path_for_namespace(
    const CodegenOptions& options, const std::vector<std::string>& parts, std::string& error_message) {
    static const PythonNameSet no_reserved_names;
    const std::optional<std::string> stem = python_name_for(
        options.root_module_stem, "root module stem", no_reserved_names, error_message);
    if (!stem.has_value()) {
        return std::nullopt;
    }
    if (parts.empty()) {
        return *stem + ".py";
    }
    return join_with(parts, '/') + "/" + *stem + ".py";
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
void collect_ancestor_init_paths(const std::vector<std::string>& parts,
                                 std::set<std::string>& init_paths) {
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

[[nodiscard]] std::string record_helper_name(std::string_view record_name,
                                             std::string_view operation) {
    return "_quarry_" + std::string(operation) + "_" + snake_case_from_pascal(record_name);
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
// dataclass field's type annotation ("bool", "int", or "float"). Enum,
// string, bytes, and supported array fields add their own planning flags;
// lower_field_encoding below rejects only unsupported categories, naming the
// record and field.

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

// --- Enum catalog -------------------------------------------------------
//
// PR-120 scope: same-namespace enum field references whose declared values
// are all non-negative -- matching the BRF spec's Enum Encoding rule and
// the C++ backend's own field-support boundary (compiler/backend/
// backend.cpp's runtime_enum_encoding). Unlike compiler/backend_c/
// backend_c.cpp's identical-in-spirit catalog, this one does not also cap
// declared values to the 32-bit signed integer range: that cap is
// backend_c's own narrower implementation choice, not a BRF-wide or
// Schema-IR-wide restriction, and Python's width bucketing already covers
// every value up to uint64::max cleanly; catalog collection can still fail on
// unsafe or colliding Python identifiers.
//
// A flat, whole-schema map from enum ir_id to the information a field
// reference needs, built once before any namespace file is planned, so a
// record field can resolve an EnumRef by id regardless of declaration
// order within the schema.

struct EnumCatalogEntry {
    std::string class_name; // sanitized bare enum name, e.g. "Status" -- no namespace
                            // prefix, unlike C's symbol-prefixed constants;
                            // Python's own package/module structure already
                            // disambiguates, matching record class naming.
    std::string owning_namespace_fqn;
    bool all_non_negative = false;
    std::string width_type_name; // meaningful only when all_non_negative,
                                 // e.g. "uint8" -- passed directly to
                                 // pack_enum()/unpack_enum() as their
                                 // `type_name` argument
    std::vector<std::pair<std::string, std::int64_t>> values; // (name, value),
                                                               // in declaration order
};

using EnumCatalog = std::unordered_map<std::uint64_t, EnumCatalogEntry>;

// Smallest unsigned scalar type name capable of representing max_value,
// matching compiler/backend_c/backend_c.cpp's enum_width_for_max_value and
// compiler/backend/backend.cpp's enum_width_for_max_value exactly -- this
// is the property that keeps enum field wire encoding byte-for-byte
// identical across all three backends.
[[nodiscard]] std::string enum_width_type_name_for_max_value(std::uint64_t max_value) {
    if (max_value <= std::numeric_limits<std::uint8_t>::max()) {
        return "uint8";
    }
    if (max_value <= std::numeric_limits<std::uint16_t>::max()) {
        return "uint16";
    }
    if (max_value <= std::numeric_limits<std::uint32_t>::max()) {
        return "uint32";
    }
    return "uint64";
}

[[nodiscard]] bool collect_enum_catalog(const NamespaceIR& ns, EnumCatalog& catalog,
                                        std::string& error_message) {
    for (const EnumIR& enum_ir : ns.enums()) {
        EnumCatalogEntry entry;
        const std::optional<std::string> python_name =
            python_name_for(enum_ir.name(), "enum", module_reserved_names(), error_message);
        if (!python_name.has_value()) {
            return false;
        }
        entry.class_name = *python_name;
        entry.owning_namespace_fqn = ns.fqn();
        entry.all_non_negative = true;

        std::uint64_t max_value = 0U;
        for (const EnumValueIR& value_ir : enum_ir.values()) {
            entry.values.emplace_back(value_ir.name(), value_ir.value());
            if (value_ir.value() < 0) {
                entry.all_non_negative = false;
            } else {
                max_value = std::max(max_value, static_cast<std::uint64_t>(value_ir.value()));
            }
        }
        if (entry.all_non_negative) {
            entry.width_type_name = enum_width_type_name_for_max_value(max_value);
        }
        catalog.emplace(enum_ir.ir_id(), std::move(entry));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_enum_catalog(child, catalog, error_message)) {
            return false;
        }
    }
    return true;
}

struct EnumFieldEncoding {
    std::string class_name;
    std::string width_type_name;
};

struct RecordCatalogEntry {
    std::string class_name; // sanitized Python class name
    std::string owning_namespace_fqn;
};

using RecordCatalog = std::unordered_map<std::uint64_t, RecordCatalogEntry>;

[[nodiscard]] bool collect_record_catalog(const NamespaceIR& ns, RecordCatalog& catalog,
                                          std::string& error_message) {
    for (const RecordIR& record_ir : ns.records()) {
        const std::optional<std::string> python_name =
            python_name_for(record_ir.name(), "record", module_reserved_names(), error_message);
        if (!python_name.has_value()) {
            return false;
        }
        catalog.emplace(record_ir.ir_id(),
                        RecordCatalogEntry{*python_name, ns.fqn()});
    }
    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_record_catalog(child, catalog, error_message)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> lower_record_reference(
    std::uint64_t target_record_ir_id, std::string_view current_namespace_fqn,
    const RecordCatalog& catalog, std::string_view context_description,
    std::string& error_message) {
    const auto catalog_it = catalog.find(target_record_ir_id);
    if (catalog_it == catalog.end()) {
        return std::nullopt;
    }
    const RecordCatalogEntry& entry = catalog_it->second;
    if (entry.owning_namespace_fqn != current_namespace_fqn) {
        std::ostringstream stream;
        stream << "backend_python: " << context_description << " references record '"
               << entry.class_name << "' declared in a different namespace ('"
               << entry.owning_namespace_fqn
               << "'); cross-namespace nested record fields are not yet supported "
                  "(see docs/design/python-backend.md)";
        error_message = stream.str();
        return std::nullopt;
    }
    return entry.class_name;
}

// Resolves an enum-typed field reference by ir_id, given the whole-schema
// enum catalog and the FQN of the namespace the reference occurs in.
// `context_description` is used only in diagnostic text (e.g. "field
// 'X.Y'"). Returns std::nullopt with `error_message` set for a
// cross-namespace or negative-valued enum; returns std::nullopt with
// `error_message` left untouched if `target_enum_ir_id` is not in the
// catalog at all -- Schema IR guarantees a resolvable reference, so callers
// treat a catalog miss as an internal-consistency fallback, not a normal
// user-facing diagnostic path.
[[nodiscard]] std::optional<EnumFieldEncoding>
lower_enum_reference(std::uint64_t target_enum_ir_id, std::string_view current_namespace_fqn,
                    const EnumCatalog& catalog, std::string_view context_description,
                    std::string& error_message) {
    const auto catalog_it = catalog.find(target_enum_ir_id);
    if (catalog_it == catalog.end()) {
        return std::nullopt;
    }
    const EnumCatalogEntry& entry = catalog_it->second;
    if (entry.owning_namespace_fqn != current_namespace_fqn) {
        std::ostringstream stream;
        stream << "backend_python: " << context_description << " references enum '"
               << entry.class_name << "' declared in a different namespace ('"
               << entry.owning_namespace_fqn
               << "'); cross-namespace enum field references are not yet supported "
                  "(see docs/design/python-backend.md)";
        error_message = stream.str();
        return std::nullopt;
    }
    if (!entry.all_non_negative) {
        std::ostringstream stream;
        stream << "backend_python: " << context_description
               << " references an enum with a negative declared value; enum fields are only "
                  "supported when every declared value is non-negative, matching the BRF "
                  "spec's Enum Encoding rule (see docs/design/python-backend.md)";
        error_message = stream.str();
        return std::nullopt;
    }
    return EnumFieldEncoding{entry.class_name, entry.width_type_name};
}

// --- Planning -------------------------------------------------------------

struct PlannedField {
    std::string name;
    std::uint32_t field_index = 0U;
    // For a scalar field: the scalar's own runtime type name (e.g.
    // "uint32"), passed to pack_scalar()/unpack_scalar(). For an enum
    // field: the enum's wire width type name (e.g. "uint8"), passed as
    // pack_enum()/unpack_enum()'s `type_name` argument. Unused for
    // string/bytes fields. For an array field (is_array), this describes
    // the *element* encoding -- reused as-is rather than duplicated into
    // separate array-specific members, mirroring backend_c.cpp's own
    // FieldEncoding, which folds its array_* members into the same
    // plain-field members the element kind would otherwise use.
    std::string runtime_type_name;
    // For a scalar field: the dataclass annotation's inner type ("bool",
    // "int", or "float"). For an enum field: the enum class name (e.g.
    // "Status") -- serves the same "what goes inside Optional[...]" role,
    // and is also pack_enum()/unpack_enum()'s `enum_cls` argument. For a
    // string field: "str". For a bytes field: "bytes". For an array field,
    // this is the *element* type hint (wrapped in list[...] at render
    // time) -- see runtime_type_name's comment above.
    std::string python_type_hint;
    bool is_enum = false;
    bool is_string = false;
    std::uint32_t string_max_bytes = 0U; // meaningful only when is_string
    bool is_bytes = false;
    std::uint32_t bytes_max_bytes = 0U; // meaningful only when is_bytes
    bool is_record = false;
    std::uint64_t record_target_ir_id = 0U; // meaningful only when is_record
    bool is_array = false;
    std::uint32_t array_max_elements = 0U; // meaningful only when is_array
};

struct PlannedRecord {
    std::uint64_t ir_id = 0U;
    std::string name;
    std::uint32_t record_id = 0U;
    std::vector<PlannedField> fields;
    std::vector<std::uint64_t> same_namespace_dependencies;
};

struct PlannedEnumValue {
    std::string name;
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string class_name;
    std::vector<PlannedEnumValue> values;
};

struct PlannedNamespaceFile {
    std::string source_namespace_fqn;
    std::string relative_module_path;
    std::vector<PlannedEnum> enums;
    std::vector<PlannedRecord> records;
};

// A namespace emits a module if it directly owns one or more records or
// enums.
[[nodiscard]] bool namespace_emits_file(const NamespaceIR& ns) {
    return ns.records_size() > 0 || ns.enums_size() > 0;
}

// Resolves one field's type: a scalar primitive, a same-namespace
// non-negative-valued enum reference, a bounded string/bytes field, a
// same-namespace nested record reference, or a bounded array of one of the
// supported non-record element kinds.
// Returns std::nullopt (with error_message set) for every unsupported
// case: a non-scalar/non-enum/non-string/non-bytes type; an enum declared
// in a different namespace; or an enum with a negative declared value.
[[nodiscard]] std::optional<PlannedField>
lower_field_encoding(const RecordIR& record_ir, const FieldIR& field_ir,
                    std::string_view current_namespace_fqn, const EnumCatalog& enum_catalog,
                    const RecordCatalog& record_catalog,
                    std::string& error_message) {
    const FieldType& type = field_ir.type();

    // Plain sequential member assignment (not a brace-enclosed designated
    // initializer) at every return site below, deliberately: GCC's
    // -Wmissing-field-initializers (part of -Wextra) flags a designated
    // initializer that omits any member, even one with a default member
    // initializer, unlike Clang -- as PlannedField's member count has grown
    // across PR-119/PR-120/PR-121, keeping every construction site in sync
    // with an ever-longer list of `.field = value` entries became exactly
    // the kind of divergence backend_c.cpp's own make_scalar_encoding
    // helper (see its identical comment) already worked around the same
    // way; caught here by the Docker/CI-equivalent validation's GCC
    // toolchain, not reproduced locally under Clang.
    if (type.kind_case() == FieldType::kEnumType) {
        const std::string context = "field '" + record_ir.fqn() + "." + field_ir.name() + "'";
        std::optional<EnumFieldEncoding> encoding = lower_enum_reference(
            type.enum_type().target_enum_ir_id(), current_namespace_fqn, enum_catalog, context,
            error_message);
        if (encoding.has_value()) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.runtime_type_name = encoding->width_type_name;
            field.python_type_hint = encoding->class_name;
            field.is_enum = true;
            return field;
        }
        if (!error_message.empty()) {
            return std::nullopt;
        }
        // Catalog miss (enum id not found at all): fall through to the
        // generic diagnostic below -- Schema IR guarantees a resolvable
        // reference, so this should not happen in practice.
    } else if (type.kind_case() == FieldType::kString) {
        // Schema validation already guarantees max_bytes > 0 and that it
        // fits uint32_t (compiler/semantic/semantic.cpp's
        // validate_positive_u32) -- nothing to re-validate here, matching
        // both existing backends' identical trust-the-upstream-invariant
        // stance for this same check.
        PlannedField field;
        field.name = field_ir.name();
        field.field_index = field_ir.field_index();
        field.python_type_hint = "str";
        field.is_string = true;
        field.string_max_bytes = type.string().max_bytes();
        return field;
    } else if (type.kind_case() == FieldType::kBytes) {
        PlannedField field;
        field.name = field_ir.name();
        field.field_index = field_ir.field_index();
        field.python_type_hint = "bytes";
        field.is_bytes = true;
        field.bytes_max_bytes = type.bytes().max_bytes();
        return field;
    } else if (type.kind_case() == FieldType::kRecord) {
        const std::string context = "field '" + record_ir.fqn() + "." + field_ir.name() + "'";
        const std::optional<std::string> record_name = lower_record_reference(
            type.record().target_record_ir_id(), current_namespace_fqn, record_catalog, context,
            error_message);
        if (record_name.has_value()) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.python_type_hint = *record_name;
            field.is_record = true;
            field.record_target_ir_id = type.record().target_record_ir_id();
            return field;
        }
        if (!error_message.empty()) {
            return std::nullopt;
        }
    } else if (type.kind_case() == FieldType::kArray) {
        // PR-122/PR-123/PR-125 scope: arrays of scalar primitives, same-namespace
        // non-negative-valued enums only -- reusing lower_scalar_field_type
        // and lower_enum_reference unchanged so cross-namespace and
        // negative-enum-value checks apply identically to array elements,
        // matching backend_c.cpp's own array-element resolution structure.
        // and PR-123's bounded string/bytes elements. PR-125 adds
        // same-namespace record elements; nested arrays remain unsupported.
        const ArrayType& array_type = type.array();
        const FieldType& element_type = array_type.element_type();
        std::optional<ScalarEncoding> scalar_encoding;
        std::optional<EnumFieldEncoding> enum_encoding;
        bool element_is_enum = false;
        if (element_type.kind_case() == FieldType::kPrimitive) {
            scalar_encoding = lower_scalar_field_type(element_type);
        } else if (element_type.kind_case() == FieldType::kEnumType) {
            const std::string context =
                "field '" + record_ir.fqn() + "." + field_ir.name() + "' array element type";
            enum_encoding = lower_enum_reference(element_type.enum_type().target_enum_ir_id(),
                                                current_namespace_fqn, enum_catalog, context,
                                                error_message);
            element_is_enum = enum_encoding.has_value();
        }
        if (scalar_encoding.has_value()) {
            // Schema validation already guarantees max_elements > 0 and that
            // it fits uint32_t (the same validate_positive_u32 call used
            // for max_bytes) -- nothing to re-validate here.
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.runtime_type_name = scalar_encoding->runtime_type_name;
            field.python_type_hint = scalar_encoding->python_type_hint;
            field.is_array = true;
            field.array_max_elements = array_type.max_elements();
            return field;
        }
        if (element_is_enum) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.runtime_type_name = enum_encoding->width_type_name;
            field.python_type_hint = enum_encoding->class_name;
            field.is_enum = true;
            field.is_array = true;
            field.array_max_elements = array_type.max_elements();
            return field;
        }
        if (element_type.kind_case() == FieldType::kString) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.python_type_hint = "str";
            field.is_string = true;
            field.string_max_bytes = element_type.string().max_bytes();
            field.is_array = true;
            field.array_max_elements = array_type.max_elements();
            return field;
        }
        if (element_type.kind_case() == FieldType::kBytes) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.python_type_hint = "bytes";
            field.is_bytes = true;
            field.bytes_max_bytes = element_type.bytes().max_bytes();
            field.is_array = true;
            field.array_max_elements = array_type.max_elements();
            return field;
        }
        if (element_type.kind_case() == FieldType::kRecord) {
            const std::string context =
                "field '" + record_ir.fqn() + "." + field_ir.name() + "' array element type";
            const std::optional<std::string> record_name = lower_record_reference(
                element_type.record().target_record_ir_id(), current_namespace_fqn,
                record_catalog, context, error_message);
            if (record_name.has_value()) {
                PlannedField field;
                field.name = field_ir.name();
                field.field_index = field_ir.field_index();
                field.python_type_hint = *record_name;
                field.is_record = true;
                field.record_target_ir_id = element_type.record().target_record_ir_id();
                field.is_array = true;
                field.array_max_elements = array_type.max_elements();
                return field;
            }
            if (!error_message.empty()) {
                return std::nullopt;
            }
        }
        if (!error_message.empty()) {
            return std::nullopt;
        }
        // Not a catalog miss (that path already set error_message above);
        // this is an array element type the Python backend does not
        // support at all (nested arrays or unresolved references).
        std::ostringstream stream;
        stream << "backend_python: field '" << record_ir.fqn() << "." << field_ir.name()
               << "' is an array whose element type the Python backend does not support yet -- "
                  "only arrays of bool, fixed-width signed/unsigned integer, f32/f64 scalar "
                  "elements, same-namespace non-negative-valued enum elements, bounded string "
                  "elements, bounded bytes elements, and same-namespace record elements are "
                  "supported (see docs/design/python-backend.md); nested arrays remain "
                  "unsupported";
        error_message = stream.str();
        return std::nullopt;
    } else {
        std::optional<ScalarEncoding> encoding = lower_scalar_field_type(type);
        if (encoding.has_value()) {
            PlannedField field;
            field.name = field_ir.name();
            field.field_index = field_ir.field_index();
            field.runtime_type_name = encoding->runtime_type_name;
            field.python_type_hint = encoding->python_type_hint;
            return field;
        }
    }

    std::ostringstream stream;
    stream << "backend_python: field '" << record_ir.fqn() << "." << field_ir.name()
           << "' has a type the Python backend does not support yet -- only bool, fixed-width "
              "signed/unsigned integer, f32/f64 scalar fields, same-namespace "
              "non-negative-valued enum fields, bounded string/bytes fields, and bounded arrays "
              "of scalar, enum, string, bytes, or same-namespace record elements are supported "
              "(see docs/design/python-backend.md); nested arrays remain unsupported";
    error_message = stream.str();
    return std::nullopt;
}

[[nodiscard]] bool order_records_topologically(std::vector<PlannedRecord>& records,
                                               std::string_view namespace_fqn,
                                               std::string& error_message) {
    std::map<std::uint64_t, std::size_t> index_by_ir_id;
    for (std::size_t index = 0; index < records.size(); ++index) {
        index_by_ir_id.emplace(records[index].ir_id, index);
    }

    std::vector<std::size_t> indegree(records.size(), 0U);
    std::vector<std::vector<std::size_t>> dependents(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        for (const std::uint64_t dependency_ir_id : records[index].same_namespace_dependencies) {
            const auto dependency_it = index_by_ir_id.find(dependency_ir_id);
            if (dependency_it == index_by_ir_id.end()) {
                error_message = "backend_python: could not resolve a same-namespace record "
                                "declaration dependency in namespace '" +
                                std::string(namespace_fqn) + "'";
                return false;
            }
            ++indegree[index];
            dependents[dependency_it->second].push_back(index);
        }
    }

    std::set<std::pair<std::size_t, std::size_t>> ready;
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.emplace(index, index);
        }
    }

    std::vector<PlannedRecord> ordered;
    ordered.reserve(records.size());
    while (!ready.empty()) {
        const auto [source_order, index] = *ready.begin();
        (void)source_order;
        ready.erase(ready.begin());
        ordered.push_back(std::move(records[index]));
        for (const std::size_t dependent_index : dependents[index]) {
            if (--indegree[dependent_index] == 0U) {
                ready.emplace(dependent_index, dependent_index);
            }
        }
    }

    if (ordered.size() != records.size()) {
        error_message = "backend_python: detected a cycle in same-namespace nested record "
                        "declaration dependencies in namespace '" + std::string(namespace_fqn) +
                        "' -- a record cannot embed itself, directly or transitively, by value";
        return false;
    }
    records = std::move(ordered);
    return true;
}

// Collects one PlannedNamespaceFile per namespace that directly owns
// records or enums, in Schema IR declaration order (pre-order namespace
// traversal), and accumulates every such namespace's ancestor __init__.py
// paths into `ancestor_init_paths`. Fails with a diagnostic naming the
// record and field for any field whose type is not one of the eleven
// supported scalar primitives or a same-namespace non-negative-valued
// enum reference.
[[nodiscard]] bool collect_namespace_files(const NamespaceIR& ns, const CodegenOptions& options,
                                          const EnumCatalog& enum_catalog,
                                          const RecordCatalog& record_catalog,
                                          std::vector<PlannedNamespaceFile>& files,
                                          std::set<std::string>& ancestor_init_paths,
                                          std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
        const std::optional<std::vector<std::string>> safe_namespace =
            sanitize_namespace_parts(ns.fqn(), error_message);
        if (!safe_namespace.has_value()) {
            return false;
        }
        const std::optional<std::string> module_path =
            module_path_for_namespace(options, *safe_namespace, error_message);
        if (!module_path.has_value()) {
            return false;
        }
        file.source_namespace_fqn = ns.fqn();
        file.relative_module_path = *module_path;
        collect_ancestor_init_paths(*safe_namespace, ancestor_init_paths);

        PythonNameSet module_symbols;

        for (const EnumIR& enum_ir : ns.enums()) {
            // Already computed once by collect_enum_catalog; reused here
            // rather than recomputed, so there is exactly one place that
            // decides an enum's rendering data.
            const EnumCatalogEntry& entry = enum_catalog.at(enum_ir.ir_id());
            if (!insert_python_name(module_symbols, entry.class_name, enum_ir.name(),
                                    "module-level class", error_message)) {
                return false;
            }
            PlannedEnum planned_enum;
            planned_enum.class_name = entry.class_name;
            PythonNameSet enum_members;
            for (const auto& [value_name, value] : entry.values) {
                const std::optional<std::string> python_member =
                    python_name_for(value_name, "enum member", enum_member_reserved_names(),
                                    error_message);
                if (!python_member.has_value() ||
                    !insert_python_name(enum_members, *python_member, value_name, "enum member",
                                        error_message)) {
                    return false;
                }
                planned_enum.values.push_back(PlannedEnumValue{.name = *python_member,
                                                               .value = value});
            }
            file.enums.push_back(std::move(planned_enum));
        }

        for (const RecordIR& record_ir : ns.records()) {
            const RecordCatalogEntry& record_entry = record_catalog.at(record_ir.ir_id());
            if (!insert_python_name(module_symbols, record_entry.class_name, record_ir.name(),
                                    "module-level class", error_message)) {
                return false;
            }
            for (const std::string_view operation : {"encode", "decode", "encoded_size"}) {
                const std::string helper = record_helper_name(record_entry.class_name, operation);
                if (!module_symbols.insert(helper).second) {
                    error_message = "backend_python: generated helper '" + helper +
                                    "' for record '" + record_ir.name() +
                                    "' collides with another module-level generated identifier";
                    return false;
                }
            }
            std::vector<PlannedField> planned_fields;
            PythonNameSet field_names;
            std::vector<std::uint64_t> dependencies;
            planned_fields.reserve(static_cast<std::size_t>(record_ir.fields_size()));
            for (const FieldIR& field_ir : record_ir.fields()) {
                std::optional<PlannedField> planned_field =
                    lower_field_encoding(record_ir, field_ir, ns.fqn(), enum_catalog,
                                        record_catalog, error_message);
                if (!planned_field.has_value()) {
                    return false;
                }
                const std::optional<std::string> python_field_name = python_name_for(
                    field_ir.name(), "field '" + record_ir.fqn() + "'", field_reserved_names(),
                    error_message);
                if (!python_field_name.has_value() ||
                    !insert_python_name(field_names, *python_field_name, field_ir.name(),
                                        "field in record '" + record_ir.fqn() + "'",
                                        error_message)) {
                    return false;
                }
                planned_field->name = *python_field_name;
                if (planned_field->is_record) {
                    dependencies.push_back(planned_field->record_target_ir_id);
                }
                planned_fields.push_back(std::move(*planned_field));
            }
            file.records.push_back(PlannedRecord{
                .ir_id = record_ir.ir_id(),
                .name = record_entry.class_name,
                .record_id = record_ir.record_id(),
                .fields = std::move(planned_fields),
                .same_namespace_dependencies = std::move(dependencies),
            });
        }

        if (!order_records_topologically(file.records, ns.fqn(), error_message)) {
            return false;
        }

        files.push_back(std::move(file));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_namespace_files(child, options, enum_catalog, record_catalog, files,
                                    ancestor_init_paths, error_message)) {
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
    EnumCatalog enum_catalog;
    if (!collect_enum_catalog(schema_ir.root_namespace(), enum_catalog, error_message)) {
        return false;
    }

    RecordCatalog record_catalog;
    if (!collect_record_catalog(schema_ir.root_namespace(), record_catalog, error_message)) {
        return false;
    }

    std::set<std::string> ancestor_init_paths;
    if (!collect_namespace_files(schema_ir.root_namespace(), options, enum_catalog, record_catalog,
                                 module_files, ancestor_init_paths, error_message)) {
        return false;
    }

    // Every generated path, including package markers, must be unique after
    // Python-specific namespace/module normalization. Shared ancestor package
    // markers were already deduplicated in the set used above.
    std::map<std::string, std::string> seen_output_paths;
    for (const std::string& path : init_file_paths) {
        seen_output_paths.emplace(path, "package ancestor");
    }
    for (const PlannedNamespaceFile& file : module_files) {
        const auto [it, inserted] =
            seen_output_paths.emplace(file.relative_module_path, file.source_namespace_fqn);
        if (!inserted) {
            error_message = "backend_python: namespaces '" + it->second + "' and '" +
                            file.source_namespace_fqn + "' both normalize to generated path '" +
                            file.relative_module_path + "'";
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
// declared field, matching PR-117's decided absent/present-via-None
// representation) whose encode/decode/encoded_size methods delegate their
// single line of implementation to the module-level helper functions below
// them (PR-118A's recommended design), plus those helper functions
// themselves, which now perform real BRF scalar encode/decode via
// quarry.runtime.python.binary_record (aliased _brf in the module preamble)
// rather than raising NotImplementedError -- PR-119's scalar milestone.
// The private _quarry_encoded_size_<name> helper is implemented as
// len(_quarry_encode_<name>(value)): always
// exactly correct by construction, at the cost of doing a full encode to
// learn a size (unlike the C/C++ backends' size-only computation) -- an
// acceptable simplicity/performance tradeoff for this first functional
// milestone, revisitable later without changing the public API.
[[nodiscard]] std::string render_record_block(const PlannedRecord& record) {
    const std::string encode_name = record_helper_name(record.name, "encode");
    const std::string decode_name = record_helper_name(record.name, "decode");
    const std::string encoded_size_name = record_helper_name(record.name, "encoded_size");
    std::ostringstream stream;
    stream << "@dataclass\n";
    stream << "class " << record.name << ":\n";
    for (const PlannedField& field : record.fields) {
        // Bare PEP 585 list[...] subscription -- no new import needed
        // beyond the already-emitted `from typing import Optional`.
        const std::string hint =
            field.is_array ? "list[" + field.python_type_hint + "]" : field.python_type_hint;
        stream << "    " << field.name << ": Optional[" << hint << "] = None\n";
    }
    stream << "\n";
    stream << "    def encode(self):\n";
    stream << "        return " << encode_name << "(self)\n";
    stream << "\n";
    stream << "    @classmethod\n";
    stream << "    def decode(cls, data):\n";
    stream << "        return " << decode_name << "(data)\n";
    stream << "\n";
    stream << "    def encoded_size(self):\n";
    stream << "        return " << encoded_size_name << "(self)\n";
    stream << "\n";
    stream << "\n";

    stream << "def " << encode_name << "(value):\n";
    stream << "    fields = []\n";
    for (const PlannedField& field : record.fields) {
        stream << "    if value." << field.name << " is not None:\n";
        if (field.is_array && field.is_record) {
            stream << "        _items = value." << field.name << "\n";
            stream << "        if len(_items) > " << field.array_max_elements << ":\n";
            stream << "            raise _brf.EncodeError(\"array length \" + str(len(_items)) + \" exceeds max_elements="
                   << field.array_max_elements << "\")\n";
            stream << "        _array_payload = bytearray()\n";
            stream << "        _brf.append_varuint(_array_payload, len(_items))\n";
            stream << "        for _item in _items:\n";
            stream << "            _encoded_item = " << record_helper_name(field.python_type_hint, "encode")
                   << "(_item)\n";
            stream << "            _brf.append_varuint(_array_payload, len(_encoded_item))\n";
            stream << "            _array_payload.extend(_encoded_item)\n";
            stream << "        fields.append((" << field.field_index
                   << ", bytes(_array_payload)))\n";
        } else if (field.is_array && field.is_string) {
            stream << "        fields.append((" << field.field_index
                   << ", _brf.pack_array_of_string(value." << field.name << ", "
                   << field.array_max_elements << ", " << field.string_max_bytes << ")))\n";
        } else if (field.is_array && field.is_bytes) {
            stream << "        fields.append((" << field.field_index
                   << ", _brf.pack_array_of_bytes(value." << field.name << ", "
                   << field.array_max_elements << ", " << field.bytes_max_bytes << ")))\n";
        } else if (field.is_array && field.is_enum) {
            stream << "        fields.append((" << field.field_index
                   << ", _brf.pack_array_of_enum(" << field.python_type_hint << ", \""
                   << field.runtime_type_name << "\", value." << field.name << ", "
                   << field.array_max_elements << ")))\n";
        } else if (field.is_array) {
            stream << "        fields.append((" << field.field_index
                   << ", _brf.pack_array_of_scalar(\"" << field.runtime_type_name
                   << "\", value." << field.name << ", " << field.array_max_elements
                   << ")))\n";
        } else if (field.is_enum) {
            stream << "        fields.append((" << field.field_index << ", _brf.pack_enum("
                   << field.python_type_hint << ", value." << field.name << ", \""
                   << field.runtime_type_name << "\")))\n";
        } else if (field.is_string) {
            stream << "        fields.append((" << field.field_index << ", _brf.pack_string("
                   << "value." << field.name << ", " << field.string_max_bytes << ")))\n";
        } else if (field.is_bytes) {
            stream << "        fields.append((" << field.field_index << ", _brf.pack_bytes("
                   << "value." << field.name << ", " << field.bytes_max_bytes << ")))\n";
        } else if (field.is_record) {
            stream << "        fields.append((" << field.field_index << ", "
                   << record_helper_name(field.python_type_hint, "encode") << "(value."
                   << field.name << ")))\n";
        } else {
            stream << "        fields.append((" << field.field_index << ", _brf.pack_scalar(\""
                   << field.runtime_type_name << "\", value." << field.name << ")))\n";
        }
    }
    stream << "    return _brf.encode_record(" << record.record_id << ", fields)\n";
    stream << "\n";
    stream << "\n";

    stream << "def " << decode_name << "(data):\n";
    stream << "    record_id, fields = _brf.parse_record(data)\n";
    stream << "    if record_id != " << record.record_id << ":\n";
    stream << "        raise _brf.DecodeError(\n";
    stream << "            f\"unexpected record id: {record_id} (expected " << record.record_id
           << ")\")\n";
    for (const PlannedField& field : record.fields) {
        stream << "    " << field.name << " = None\n";
        stream << "    if " << field.field_index << " in fields:\n";
        if (field.is_array && field.is_record) {
            stream << "        _array_data = fields[" << field.field_index << "]\n";
            stream << "        try:\n";
            stream << "            _count, _offset = _brf.read_varuint(_array_data, 0)\n";
            stream << "        except _brf.DecodeError as _error:\n";
            stream << "            raise _brf.DecodeError(\"malformed array count varuint at byte "
                   "offset 0: \" + str(_error)) from _error\n";
            stream << "        if _count > " << field.array_max_elements << ":\n";
            stream << "            raise _brf.DecodeError(\"array count \" + str(_count) + \" exceeds max_elements="
                   << field.array_max_elements << "\")\n";
            stream << "        " << field.name << " = []\n";
            stream << "        for _index in range(_count):\n";
            stream << "            _length_offset = _offset\n";
            stream << "            try:\n";
            stream << "                _element_length, _offset = _brf.read_varuint(_array_data, _offset)\n";
            stream << "            except _brf.DecodeError as _error:\n";
            stream << "                raise _brf.DecodeError('malformed element length varuint for ' + str(_index) + ' at byte offset ' + str(_length_offset) + ': ' + str(_error)) from _error\n";
            stream << "            if _element_length > len(_array_data) - _offset:\n";
            stream << "                raise _brf.DecodeError('truncated array element ' + str(_index) + ' payload at byte offset ' + str(_offset))\n";
            stream << "            _element_end = _offset + _element_length\n";
            stream << "            try:\n";
            stream << "                " << field.name << ".append("
                   << record_helper_name(field.python_type_hint, "decode")
                   << "(_array_data[_offset:_element_end]))\n";
            stream << "            except _brf.DecodeError as _error:\n";
            stream << "                raise _brf.DecodeError('array element ' + str(_index) + ' at byte offset ' + str(_offset) + ': ' + str(_error)) from _error\n";
            stream << "            _offset = _element_end\n";
            stream << "        if _offset != len(_array_data):\n";
            stream << "            raise _brf.DecodeError(\"trailing bytes in record array payload at "
                   "byte offset \" + str(_offset))\n";
        } else if (field.is_array && field.is_string) {
            stream << "        " << field.name << " = _brf.unpack_array_of_string(fields["
                   << field.field_index << "], " << field.array_max_elements << ", "
                   << field.string_max_bytes << ")\n";
        } else if (field.is_array && field.is_bytes) {
            stream << "        " << field.name << " = _brf.unpack_array_of_bytes(fields["
                   << field.field_index << "], " << field.array_max_elements << ", "
                   << field.bytes_max_bytes << ")\n";
        } else if (field.is_array && field.is_enum) {
            stream << "        " << field.name << " = _brf.unpack_array_of_enum("
                   << field.python_type_hint << ", \"" << field.runtime_type_name
                   << "\", fields[" << field.field_index << "], " << field.array_max_elements
                   << ")\n";
        } else if (field.is_array) {
            stream << "        " << field.name << " = _brf.unpack_array_of_scalar(\""
                   << field.runtime_type_name << "\", fields[" << field.field_index << "], "
                   << field.array_max_elements << ")\n";
        } else if (field.is_enum) {
            stream << "        " << field.name << " = _brf.unpack_enum(" << field.python_type_hint
                   << ", \"" << field.runtime_type_name << "\", fields[" << field.field_index
                   << "])\n";
        } else if (field.is_string) {
            stream << "        " << field.name << " = _brf.unpack_string(fields["
                   << field.field_index << "], " << field.string_max_bytes << ")\n";
        } else if (field.is_bytes) {
            stream << "        " << field.name << " = _brf.unpack_bytes(fields["
                   << field.field_index << "], " << field.bytes_max_bytes << ")\n";
        } else if (field.is_record) {
            stream << "        " << field.name << " = "
                   << record_helper_name(field.python_type_hint, "decode") << "(fields["
                   << field.field_index << "])\n";
        } else {
            stream << "        " << field.name << " = _brf.unpack_scalar(\""
                   << field.runtime_type_name << "\", fields[" << field.field_index << "])\n";
        }
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

    stream << "def " << encoded_size_name << "(value):\n";
    stream << "    return len(" << encode_name << "(value))\n";
    return stream.str();
}

// Renders one enum as an `enum.IntEnum` subclass. Rendered before any
// record in the same file (see render_module): a record's dataclass field
// annotation (`Optional[Status] = None`) evaluates eagerly when the class
// body executes, so the enum class must already be defined at that point
// -- there is no forward-reference or cycle concern here the way nested
// records need topological sorting for in the C/C++ backends, since an
// enum is a simple leaf value collection, never itself embedding a field.
[[nodiscard]] std::string render_enum_block(const PlannedEnum& enum_ir) {
    std::ostringstream stream;
    stream << "class " << enum_ir.class_name << "(IntEnum):\n";
    for (const PlannedEnumValue& value : enum_ir.values) {
        stream << "    " << value.name << " = " << value.value << "\n";
    }
    return stream.str();
}

// A namespace's generated module: a file-level epoch-check preamble
// (imports the runtime's compatibility constant and raises ImportError on
// mismatch, mirroring the philosophy -- not the literal compile-time
// mechanism -- of the C/C++ generated-code epoch guards), followed by one
// enum class per declared enum, then one record block per declared record.
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
    if (!file.enums.empty()) {
        stream << "from enum import IntEnum\n";
    }
    stream << "from typing import Optional\n";

    bool first_block = true;
    for (const PlannedEnum& enum_ir : file.enums) {
        stream << "\n";
        if (!first_block) {
            stream << "\n";
        }
        stream << render_enum_block(enum_ir);
        first_block = false;
    }
    for (const PlannedRecord& record : file.records) {
        stream << "\n";
        if (!first_block) {
            stream << "\n";
        }
        stream << render_record_block(record);
        first_block = false;
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
