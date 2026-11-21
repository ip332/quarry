#include "compiler/backend/backend.hpp"
#include "compiler/support/generated_names.hpp"
#include "compiler/backend/generated_code_api_version.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_c/generated_code_api_version_c.hpp"
#include "compiler/backend_go/backend_go.hpp"
#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "compiler/support/source_location.hpp"
#include "quarry/version.hpp"
#include "tools/schema_compiler/configuration_preparation.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace backend = quarry::compiler::backend;
namespace backend_c = quarry::compiler::backend_c;
namespace backend_go = quarry::compiler::backend_go;
namespace backend_python = quarry::compiler::backend_python;
namespace context = quarry::compiler::context;
namespace diagnostics = quarry::compiler::diagnostics;
namespace frontend = quarry::compiler::frontend;
using quarry::tools::schema_compiler::generate_bindings;
using quarry::tools::schema_compiler::plan_outputs;
using quarry::tools::schema_compiler::prepare_configuration;
using quarry::tools::schema_compiler::PreparedConfiguration;

constexpr int exit_success = 0;
constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

enum class Language {
    Cpp,
    C,
    Python,
    Go,
};

struct CommandLine {
    bool show_help = false;
    bool show_version = false;
    bool show_generated_code_api_version = false;
    bool print_schema_id = false;
    bool describe = false;
    bool list_outputs = false;
    bool list_inputs = false;
    bool c_optimize_size = false;
    std::vector<std::string> c_optimize_size_namespaces;
    bool cpp_c_storage = false;
    bool qbs_reflective = false;
    std::string qbs_output;
    std::string go_import_prefix;
    Language language = Language::Cpp;
    std::string input_path;
    backend::CodegenOptions codegen_options;
    // --generate: namespaces to emit (empty: the root's default).
    std::vector<std::string> generate_namespaces;
};

[[nodiscard]] std::string usage_text() {
    return "usage: quarry-schema-compiler [options] INPUT\n"
           "\n"
           "Options:\n"
           "  -o, --output-directory PATH  Directory for generated files (default: generated)\n"
           "      --root-file-stem NAME     Root namespace file stem (default: schema)\n"
           "      --file-extension EXT      Generated file extension for --language cpp\n"
           "                                (default: .generated.hpp)\n"
           "      --language {cpp,c,python,go}\n"
           "                                Target backend language (default: cpp)\n"
           "      --cpp-storage {owning,c}  C++ owning containers or fixed C storage\n"
           "      --c-optimize-size[=NS,...] Pack C presence flags for all or exact namespaces\n"
           "      --go-import-prefix PATH   Go import path of the output directory, for\n"
           "                                cross-namespace imports with --language go\n"
           "      --generate NS[,NS...]     Emit only these namespaces (repeatable). Default: the\n"
           "                                root's namespace, or every namespace of a\n"
           "                                configuration root. IDs and QBS never change.\n"
           "      --emit-qbs PATH           Emit the resolved schema as a deterministic QBS image\n"
           "      --qbs-reflective          Include reflective record, field, and enum names in "
           "QBS\n"
           "      --list-inputs             Print all resolved schema input paths without writing "
           "files\n"
           "      --list-outputs            Print generated output paths without writing files\n"
           "      --print-generated-code-api-version\n"
           "                                Print the generated-code API compatibility version and "
           "exit\n"
           "      --print-schema-id         Print the resolved configuration's 128-bit schema_id\n"
           "      --name-prefix NAME        Nest generated source names and paths beneath NAME\n"
           "      --describe                Describe the full configuration without writing files\n"
           "      --version                 Show version information\n"
           "  -h, --help                    Show this help text\n";
}

void print_usage(std::ostream& output) { output << usage_text(); }

void print_version(std::ostream& output) {
    output << "quarry-schema-compiler " << QUARRY_VERSION_DISPLAY << " ("
           << (std::string_view(QUARRY_GIT_SHA).empty() ? "Git SHA unavailable" : QUARRY_GIT_SHA)
           << ", ";
    if (std::string_view(QUARRY_RELEASE_TAG) == "untagged") {
        output << "untagged";
    } else {
        output << "release tag " << QUARRY_RELEASE_TAG;
    }
    output << ")\n";
}

void print_generated_code_api_version(std::ostream& output) {
    output << quarry::compiler::backend::kGeneratedCodeApiVersion << '\n';
}

void print_schema_id(std::ostream& output,
                     const quarry::compiler::support::ConfigurationIdentity& identity) {
    output << quarry::compiler::support::schema_id_hex(identity) << '\n';
}

// Metadata only: paths are display labels, never host-specific canonical paths.
[[nodiscard]] std::string description_label(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const unsigned char byte : value) {
        if (byte == '\\') {
            text += "\\\\";
        } else if (byte < 32U || byte == 127U) {
            text += "\\x";
            text += digits[byte >> 4U];
            text += digits[byte & 15U];
        } else {
            text += static_cast<char>(byte);
        }
    }
    return text;
}

void print_description(const CommandLine& command_line,
                       const context::CompilerContext& compiler_context,
                       const quarry::compiler::schema_ir::SchemaIrModel& ir,
                       const PreparedConfiguration& prepared, std::ostream& output) {
    struct Record {
        std::uint32_t id;
        std::string_view fqn;
    };
    std::map<std::uint32_t, std::string_view> domains;
    std::vector<Record> records;
    std::vector<const quarry::schema_ir::NamespaceIR*> pending{&ir.root_namespace()};
    while (!pending.empty()) {
        const auto* ns = pending.back();
        pending.pop_back();
        if (!ns->records().empty()) {
            // Extract the existing domain component; do not allocate/reassign IDs.
            domains.emplace(ns->records(0).record_id() >> 16U, ns->fqn());
        }
        for (const auto& record : ns->records()) {
            records.push_back({record.record_id(), record.fqn()});
        }
        for (const auto& child : ns->namespaces()) {
            pending.push_back(&child);
        }
    }
    std::sort(records.begin(), records.end(), [](const Record& left, const Record& right) {
        return std::tie(left.id, left.fqn) < std::tie(right.id, right.fqn);
    });
    std::ostringstream text;
    text.imbue(std::locale::classic());
    for (const auto& unit : compiler_context.source_units()) {
        if (unit.is_root && unit.is_configuration) {
            text << "configuration: " << description_label(unit.configuration_name) << '\n';
        }
    }
    auto root = std::filesystem::path(command_line.input_path).lexically_normal();
    if (root.is_absolute()) {
        root = root.filename();
    }
    text << "root: " << description_label(root.generic_string()) << '\n';
    const auto& identity = *prepared.identity;
    text << "schema_id: " << quarry::compiler::support::schema_id_hex(identity)
         << " (algorithm " << static_cast<unsigned int>(identity.algorithm) << ": sha256-128)\n";
    text << "compiler: ";
    print_version(text);
    text << "formats: brf " << static_cast<unsigned int>(quarry::compiler::qbs::kBrfFormatVersion)
         << ", qbs " << static_cast<unsigned int>(quarry::compiler::qbs::kQbsFormatVersion)
         << ", schema-ir " << ir.schema_ir_version() << '\n';
    text << "generated-code epochs: cpp " << backend::kGeneratedCodeApiVersion
         << ", c " << backend_c::kGeneratedCodeApiVersionC
         << ", python " << QUARRY_SCHEMA_COMPILER_PYTHON_API_EPOCH
         << ", go " << backend_go::kGeneratedCodeApiVersionGo << '\n';
    text << "domains: " << domains.size() << '\n';
    for (const auto& [id, fqn] : domains) {
        text << "  0x" << std::hex << std::setfill('0') << std::setw(4) << id << ' '
             << (fqn.empty() ? "<root>" : fqn) << '\n';
    }
    text << std::dec << "records: " << records.size() << '\n';
    for (const auto& record : records) {
        text << "  0x" << std::hex << std::setw(8) << record.id << ' ' << record.fqn << '\n';
    }
    output << text.str();
}

[[nodiscard]] bool option_requires_value(std::string_view option) {
    return option == "-o" || option == "--output-directory" || option == "--root-file-stem" ||
           option == "--file-extension" || option == "--language" || option == "--emit-qbs" ||
           option == "--cpp-storage" || option == "--go-import-prefix" || option == "--generate" ||
           option == "--name-prefix";
}

[[nodiscard]] std::optional<CommandLine> parse_command_line(int argc, char** argv,
                                                            std::ostream& errors) {
    CommandLine command_line;
    bool saw_name_prefix = false;
    bool saw_output_directory = false;
    bool saw_root_file_stem = false;
    bool saw_file_extension = false;
    bool saw_language = false;
    bool saw_cpp_storage = false;
    bool saw_qbs_output = false;
    bool saw_qbs_reflective = false;
    bool saw_go_import_prefix = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "-h" || argument == "--help") {
            command_line.show_help = true;
            continue;
        }
        if (argument == "--version") {
            command_line.show_version = true;
            continue;
        }
        if (argument == "--print-generated-code-api-version") {
            if (command_line.show_generated_code_api_version) {
                errors << "error: duplicate --print-generated-code-api-version option\n";
                return std::nullopt;
            }
            command_line.show_generated_code_api_version = true;
            continue;
        }
        if (argument == "--describe") {
            if (command_line.describe) {
                errors << "error: duplicate --describe option\n";
                return std::nullopt;
            }
            command_line.describe = true;
            continue;
        }
        if (argument == "--print-schema-id") {
            if (command_line.print_schema_id) {
                errors << "error: duplicate --print-schema-id option\n";
                return std::nullopt;
            }
            command_line.print_schema_id = true;
            continue;
        }
        if (argument == "--c-optimize-size" || argument.starts_with("--c-optimize-size=")) {
            if (command_line.c_optimize_size) {
                errors << "error: duplicate --c-optimize-size option\n";
                return std::nullopt;
            }
            command_line.c_optimize_size = true;
            if (argument != "--c-optimize-size") {
                std::string_view names =
                    argument.substr(std::string_view("--c-optimize-size=").size());
                if (names.empty()) {
                    errors << "error: --c-optimize-size= requires a nonempty namespace list; "
                              "omit the option for default storage or use the bare flag for all\n";
                    return std::nullopt;
                }
                for (;;) {
                    const auto comma = names.find(',');
                    command_line.c_optimize_size_namespaces.emplace_back(names.substr(0, comma));
                    if (comma == std::string_view::npos)
                        break;
                    names.remove_prefix(comma + 1);
                }
            }
            continue;
        }
        if (argument == "--list-inputs") {
            if (command_line.list_inputs) {
                errors << "error: duplicate --list-inputs option\n";
                return std::nullopt;
            }
            command_line.list_inputs = true;
            continue;
        }
        if (argument == "--list-outputs") {
            command_line.list_outputs = true;
            continue;
        }
        if (argument == "--qbs-reflective") {
            if (saw_qbs_reflective) {
                errors << "error: duplicate --qbs-reflective option\n";
                return std::nullopt;
            }
            saw_qbs_reflective = true;
            command_line.qbs_reflective = true;
            continue;
        }

        if (option_requires_value(argument)) {
            if (index + 1 >= argc) {
                errors << "error: missing value for " << argument << '\n';
                return std::nullopt;
            }

            const std::string value = argv[++index];
            if (argument == "--generate") {
                std::size_t begin = 0U;
                while (begin <= value.size()) {
                    const std::size_t end = std::min(value.find(',', begin), value.size());
                    const std::string name = value.substr(begin, end - begin);
                    if (name.empty()) {
                        errors << "error: empty namespace in --generate " << value << '\n';
                        return std::nullopt;
                    }
                    if (std::find(command_line.generate_namespaces.begin(),
                                  command_line.generate_namespaces.end(),
                                  name) == command_line.generate_namespaces.end()) {
                        command_line.generate_namespaces.push_back(name);
                    }
                    begin = end + 1U;
                }
                continue;
            }
            if (argument == "--cpp-storage") {
                if (saw_cpp_storage || (value != "owning" && value != "c")) {
                    errors << "error: --cpp-storage must be specified once as owning or c\n";
                    return std::nullopt;
                }
                saw_cpp_storage = true;
                command_line.cpp_c_storage = value == "c";
                continue;
            }
            if (value.empty()) {
                errors << "error: empty value for " << argument << '\n';
                return std::nullopt;
            }

            if (argument == "--name-prefix") {
                if (saw_name_prefix || !quarry::compiler::support::valid_name_prefix(value)) {
                    errors << "error: --name-prefix must be specified once as an unreserved "
                              "portable identifier (letters, digits and underscores; no leading "
                              "underscore, double underscore, keyword or reserved library/generator name)\n";
                    return std::nullopt;
                }
                saw_name_prefix = true;
                command_line.codegen_options.name_prefix = value;
            } else if (argument == "-o" || argument == "--output-directory") {
                if (saw_output_directory) {
                    errors << "error: duplicate output-directory option\n";
                    return std::nullopt;
                }
                saw_output_directory = true;
                command_line.codegen_options.output_directory = value;
            } else if (argument == "--root-file-stem") {
                if (saw_root_file_stem) {
                    errors << "error: duplicate root-file-stem option\n";
                    return std::nullopt;
                }
                saw_root_file_stem = true;
                command_line.codegen_options.root_file_stem = value;
            } else if (argument == "--file-extension") {
                if (saw_file_extension) {
                    errors << "error: duplicate file-extension option\n";
                    return std::nullopt;
                }
                saw_file_extension = true;
                command_line.codegen_options.file_extension = value;
            } else {
                if (argument == "--emit-qbs") {
                    if (saw_qbs_output) {
                        errors << "error: duplicate --emit-qbs option\n";
                        return std::nullopt;
                    }
                    saw_qbs_output = true;
                    command_line.qbs_output = value;
                    continue;
                }
                if (argument == "--go-import-prefix") {
                    if (saw_go_import_prefix) {
                        errors << "error: duplicate --go-import-prefix option\n";
                        return std::nullopt;
                    }
                    saw_go_import_prefix = true;
                    command_line.go_import_prefix = value;
                    continue;
                }
                if (saw_language) {
                    errors << "error: duplicate language option\n";
                    return std::nullopt;
                }
                saw_language = true;
                if (value == "cpp") {
                    command_line.language = Language::Cpp;
                } else if (value == "c") {
                    command_line.language = Language::C;
                } else if (value == "python") {
                    command_line.language = Language::Python;
                } else if (value == "go") {
                    command_line.language = Language::Go;
                } else {
                    errors << "error: invalid value for --language: " << value
                           << " (expected 'cpp', 'c', 'python', or 'go')\n";
                    return std::nullopt;
                }
            }
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            errors << "error: unknown option " << argument << '\n';
            return std::nullopt;
        }

        if (!command_line.input_path.empty()) {
            errors << "error: expected exactly one input file\n";
            return std::nullopt;
        }
        command_line.input_path = std::string(argument);
    }

    if (command_line.describe) {
        if (!command_line.show_help &&
            (command_line.show_version || command_line.show_generated_code_api_version ||
             command_line.print_schema_id || command_line.list_inputs || command_line.list_outputs ||
             saw_qbs_output || saw_qbs_reflective)) {
            errors << "error: --describe cannot be combined with other query or QBS operations\n";
            return std::nullopt;
        }
        if (!command_line.show_help && command_line.input_path.empty()) {
            errors << "error: expected exactly one input file\n";
            return std::nullopt;
        }
        // Syntax was checked above. Backend/output options have no meaning in
        // a full-configuration metadata query, including --generate selection.
        return command_line;
    }

    if (command_line.show_generated_code_api_version) {
        const bool has_generation_arguments =
            !command_line.codegen_options.name_prefix.empty() ||
            command_line.list_inputs || command_line.list_outputs ||
            !command_line.input_path.empty() ||
            command_line.codegen_options.output_directory != "generated" ||
            command_line.codegen_options.root_file_stem != "schema" ||
            command_line.codegen_options.file_extension != ".generated.hpp" ||
            command_line.language != Language::Cpp || !command_line.qbs_output.empty() ||
            command_line.qbs_reflective || saw_cpp_storage || saw_go_import_prefix ||
            !command_line.generate_namespaces.empty() || command_line.print_schema_id;
        if (has_generation_arguments) {
            errors << "error: --print-generated-code-api-version does not accept generation "
                      "options or an input file\n";
            return std::nullopt;
        }
    }

    if (command_line.print_schema_id &&
        (command_line.list_inputs || command_line.list_outputs || saw_output_directory ||
         saw_root_file_stem || saw_file_extension || saw_language || saw_cpp_storage ||
         command_line.c_optimize_size || !command_line.qbs_output.empty() ||
         command_line.qbs_reflective || saw_go_import_prefix)) {
        errors << "error: --print-schema-id does not accept output or QBS options\n";
        return std::nullopt;
    }

    if (command_line.list_inputs &&
        (command_line.print_schema_id || command_line.list_outputs || saw_output_directory ||
         saw_root_file_stem || saw_file_extension || saw_language || saw_cpp_storage ||
         command_line.c_optimize_size || saw_qbs_output || saw_qbs_reflective ||
         saw_go_import_prefix)) {
        errors << "error: --list-inputs does not accept output or QBS options\n";
        return std::nullopt;
    }

    if (saw_cpp_storage && command_line.language != Language::Cpp) {
        errors << "error: --cpp-storage requires --language cpp\n";
        return std::nullopt;
    }
    if (command_line.cpp_c_storage && saw_file_extension) {
        errors << "error: --file-extension is not supported with --cpp-storage c\n";
        return std::nullopt;
    }
    if (command_line.c_optimize_size && command_line.language != Language::C &&
        !command_line.cpp_c_storage) {
        errors << "error: --c-optimize-size requires --language c or --cpp-storage c\n";
        return std::nullopt;
    }

    if (command_line.language == Language::C && saw_file_extension) {
        errors << "error: --file-extension is not supported with --language c\n";
        return std::nullopt;
    }

    if (command_line.qbs_reflective && command_line.qbs_output.empty() &&
        !command_line.show_help && !command_line.show_version &&
        !command_line.show_generated_code_api_version) {
        errors << "error: --qbs-reflective requires --emit-qbs\n";
        return std::nullopt;
    }

    if (command_line.language == Language::Python && saw_file_extension) {
        errors << "error: --file-extension is not supported with --language python\n";
        return std::nullopt;
    }
    if (command_line.language == Language::Go && saw_file_extension) {
        errors << "error: --file-extension is not supported with --language go\n";
        return std::nullopt;
    }
    if (saw_go_import_prefix && command_line.language != Language::Go) {
        errors << "error: --go-import-prefix requires --language go\n";
        return std::nullopt;
    }

    if (!command_line.show_help && !command_line.show_version &&
        !command_line.show_generated_code_api_version && command_line.input_path.empty()) {
        errors << "error: expected exactly one input file\n";
        return std::nullopt;
    }

    return command_line;
}

[[nodiscard]] std::filesystem::path normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

[[nodiscard]] bool is_path_inside(const std::filesystem::path& root,
                                  const std::filesystem::path& path) {
    const std::filesystem::path normalized_root = normalized_absolute_path(root);
    const std::filesystem::path normalized_path = normalized_absolute_path(path);
    const std::filesystem::path relative = normalized_path.lexically_relative(normalized_root);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    return std::none_of(relative.begin(), relative.end(), [](const std::filesystem::path& part) {
        return part == "..";
    });
}

[[nodiscard]] std::filesystem::path temp_path_for(const std::filesystem::path& output_path) {
    return output_path.parent_path() /
           (output_path.filename().string() + ".tmp-quarry-schema-compiler");
}

// Templated on the generated-file type so this file-writing/safety logic is
// shared by every backend's output (a CLI-level, backend-agnostic concern)
// without either backend depending on the other's types.
template <typename GeneratedFileT>
[[nodiscard]] bool write_generated_file(const GeneratedFileT& file,
                                        const std::filesystem::path& output_root,
                                        std::ostream& errors) {
    const std::filesystem::path output_path{file.path};
    if (!is_path_inside(output_root, output_path)) {
        errors << "error: backend generated path outside output directory: " << file.path << '\n';
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        errors << "error: failed to create output directory " << output_path.parent_path().string()
               << ": " << error.message() << '\n';
        return false;
    }

    const std::filesystem::path temporary_path = temp_path_for(output_path);
    {
        std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
        if (!output) {
            errors << "error: failed to open temporary output file " << temporary_path.string()
                   << '\n';
            return false;
        }
        output << file.content;
        if (!output) {
            errors << "error: failed to write temporary output file " << temporary_path.string()
                   << '\n';
            std::filesystem::remove(temporary_path, error);
            return false;
        }
    }

    std::filesystem::rename(temporary_path, output_path, error);
    if (error) {
        errors << "error: failed to write output file " << output_path.string() << ": "
               << error.message() << '\n';
        std::filesystem::remove(temporary_path, error);
        return false;
    }

    return true;
}

[[nodiscard]] bool write_qbs_file(const std::string& path, const std::vector<std::uint8_t>& bytes,
                                  std::ostream& errors) {
    const std::filesystem::path output_path{path};
    std::error_code error;
    if (!output_path.parent_path().empty())
        std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        errors << "error: failed to create output directory " << output_path.parent_path().string()
               << ": " << error.message() << '\n';
        return false;
    }
    const auto temporary_path = temp_path_for(output_path);
    std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        errors << "error: failed to open QBS output file " << temporary_path.string() << '\n';
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        errors << "error: failed to write QBS output file " << temporary_path.string() << '\n';
        std::filesystem::remove(temporary_path, error);
        return false;
    }
    std::filesystem::rename(temporary_path, output_path, error);
    if (error) {
        errors << "error: failed to write QBS output file " << output_path.string() << ": "
               << error.message() << '\n';
        std::filesystem::remove(temporary_path, error);
        return false;
    }
    return true;
}

template <typename CodegenResultT>
[[nodiscard]] bool write_generated_files(const CodegenResultT& result,
                                         const std::string& output_directory,
                                         std::ostream& errors) {
    const std::filesystem::path output_root{output_directory};
    std::set<std::filesystem::path> seen_paths;
    for (const auto& file : result.files) {
        const std::filesystem::path normalized_path =
            normalized_absolute_path(std::filesystem::path{file.path});
        if (!seen_paths.insert(normalized_path).second) {
            errors << "error: backend generated duplicate output path: " << file.path << '\n';
            return false;
        }
        if (!is_path_inside(output_root, file.path)) {
            errors << "error: backend generated path outside output directory: " << file.path
                   << '\n';
            return false;
        }
    }

    for (const auto& file : result.files) {
        if (!write_generated_file(file, output_root, errors)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] int run_schema_compiler(const CommandLine& command_line, std::ostream& output,
                                      std::ostream& errors) {
    if (command_line.show_help) {
        print_usage(output);
        return exit_success;
    }
    if (command_line.show_version) {
        print_version(output);
        return exit_success;
    }
    if (command_line.show_generated_code_api_version) {
        print_generated_code_api_version(output);
        return exit_success;
    }

    context::CompilerContext compiler_context;
    const auto read_result =
        compiler_context.file_system().read_text_file(command_line.input_path);
    if (!read_result.found) {
        errors << "error: failed to read input file " << command_line.input_path << '\n';
        return exit_failure;
    }

    const std::string source_path =
        compiler_context.file_system().normalize_path(command_line.input_path);
    const auto source_file_id =
        compiler_context.source_manager().add_source(source_path, read_result.text);

    diagnostics::DiagnosticEngine diagnostic_engine;
    frontend::YamlCompiler compiler;
    const frontend::YamlCompilationResult compilation_result =
        compiler.compile(source_file_id, compiler_context, diagnostic_engine,
                         quarry::compiler::output_planning::GenerationSelection{
                             .namespaces = command_line.describe ? std::vector<std::string>{}
                                                                : command_line.generate_namespaces});

    if (!diagnostic_engine.empty()) {
        errors << diagnostics::DiagnosticFormatter::format_all(
            diagnostic_engine, compiler_context.source_manager());
        errors << '\n';
    }

    if (!compilation_result.succeeded()) {
        return exit_failure;
    }

    if (!command_line.codegen_options.name_prefix.empty()) {
        std::vector<const quarry::schema_ir::NamespaceIR*> pending{
            &compilation_result.schema_ir->root_namespace()};
        while (!pending.empty()) {
            const auto* ns = pending.back();
            pending.pop_back();
            const auto first = ns->fqn().substr(0, ns->fqn().find('.'));
            if (first == command_line.codegen_options.name_prefix) {
                errors << "error: --name-prefix '" << first
                       << "' conflicts with a namespace's first segment in the complete configuration\n";
                return exit_usage;
            }
            for (const auto& child : ns->namespaces()) {
                pending.push_back(&child);
            }
        }
    }

    if (command_line.list_inputs) {
        // Every unit is listed at its canonical path, which changes when its
        // content does. The root and each import are also listed at the path
        // they were named by (resolved lexically, symbolic links kept), which
        // changes when a link along it is retargeted to a different schema.
        // A named path is listed only while it still refers to the resolved
        // file: lexical `..` after a directory link can name another file.
        std::set<std::string> inputs;
        const auto add_named_path = [&inputs](const std::filesystem::path& named,
                                              const std::string& resolved) {
            std::error_code error;
            auto absolute = std::filesystem::absolute(named, error);
            if (error) {
                return;
            }
            absolute = absolute.lexically_normal();
            if (std::filesystem::equivalent(absolute, resolved, error) && !error) {
                inputs.insert(absolute.string());
            }
        };
        for (const auto& unit : compiler_context.source_units()) {
            inputs.insert(unit.canonical_path);
            if (unit.is_root) {
                add_named_path(command_line.input_path, unit.canonical_path);
            }
            // Imports resolve against the importing unit's canonical directory.
            const auto base = std::filesystem::path(unit.canonical_path).parent_path();
            for (const auto& import : unit.imports) {
                add_named_path(base / import.requested_path, import.resolved_path);
            }
        }
        for (const auto& path : inputs) {
            if (path.find_first_of("\r\n") != std::string::npos) {
                errors << "error: schema input path contains a newline or carriage return\n";
                return exit_failure;
            }
        }
        for (const auto& path : inputs) {
            output << path << '\n';
        }
        return exit_success;
    }

    if (!command_line.describe && command_line.c_optimize_size) {
        backend_c::CodegenOptions storage_options;
        storage_options.optimize_size_namespaces = command_line.c_optimize_size_namespaces;
        if (const auto error = backend_c::validate_storage_options(*compilation_result.schema_ir,
                                                                   storage_options)) {
            errors << "backend error: " << *error << '\n';
            return exit_failure;
        }
    }

    // Prepare the resolved configuration once: its authoritative BRF layout,
    // and its identity only where an output depends on it. --emit-qbs hashes
    // inside the QBS builder; --list-outputs needs no identity at all.
    const bool identity_required = command_line.describe || command_line.print_schema_id ||
                                   (!command_line.list_outputs && command_line.qbs_output.empty());
    diagnostics::DiagnosticEngine layout_diagnostics;
    const std::optional<PreparedConfiguration> prepared =
        prepare_configuration(*compilation_result.schema_ir, identity_required, layout_diagnostics);
    const auto print_layout_diagnostics = [&] {
        errors << diagnostics::DiagnosticFormatter::format_all(layout_diagnostics,
                                                               compiler_context.source_manager());
    };

    if (command_line.describe) {
        if (!prepared.has_value()) {
            print_layout_diagnostics();
            return exit_failure;
        }
        print_description(command_line, compiler_context, *compilation_result.schema_ir, *prepared,
                          output);
        return exit_success;
    }

    if (command_line.print_schema_id) {
        if (!prepared.has_value()) {
            print_layout_diagnostics();
            return exit_failure;
        }
        print_schema_id(output, *prepared->identity);
        return exit_success;
    }

    if (!command_line.qbs_output.empty()) {
        if (!prepared.has_value()) {
            print_layout_diagnostics();
            return exit_failure;
        }
        diagnostics::DiagnosticEngine qbs_diagnostics;
        quarry::compiler::qbs::QbsModelBuilder builder;
        const quarry::compiler::qbs::QbsBuildOptions qbs_options{
            .mode = command_line.qbs_reflective ? quarry::compiler::qbs::BuildMode::Reflective
                                                : quarry::compiler::qbs::BuildMode::Minimal,
            .compute_configuration_identity = !command_line.list_outputs};
        auto qbs_model = builder.build(*compilation_result.schema_ir, prepared->layout, qbs_options,
                                       qbs_diagnostics);
        if (!qbs_model.has_value()) {
            errors << diagnostics::DiagnosticFormatter::format_all(
                qbs_diagnostics, compiler_context.source_manager());
            return exit_failure;
        }
        if (command_line.list_outputs) {
            output << command_line.qbs_output << '\n';
            return exit_success;
        }
        if (!qbs_model->configuration_identity.has_value()) {
            errors << "error: failed to compute configuration identity\n";
            return exit_failure;
        }
        auto serialized = quarry::compiler::qbs::serialize_qbs(*qbs_model, qbs_diagnostics);
        if (!serialized || !write_qbs_file(command_line.qbs_output, serialized->bytes, errors)) {
            if (!qbs_diagnostics.empty())
                errors << diagnostics::DiagnosticFormatter::format_all(
                    qbs_diagnostics, compiler_context.source_manager());
            return exit_failure;
        }
        return exit_success;
    }

    const auto& schema_ir = *compilation_result.schema_ir;
    const auto& output_plan = *compilation_result.output_plan;
    // Backends plan and generate from the prepared layout. If layout
    // computation failed there is no prepared configuration; the backend's
    // self-contained entry point then reports the failure with its established
    // backend-specific diagnostic.
    const auto plan = [&](const auto& backend, const auto& options) {
        return prepared.has_value()
                   ? plan_outputs(backend, schema_ir, *prepared, options, output_plan)
                   : backend.plan(schema_ir, options, &output_plan);
    };
    const auto generate = [&](const auto& backend, const auto& options) {
        return prepared.has_value()
                   ? generate_bindings(backend, schema_ir, *prepared, options, output_plan)
                   : backend.generate(schema_ir, options, &output_plan);
    };
    // Generated output is written only for a successfully prepared configuration.
    const auto require_prepared = [&] {
        if (prepared.has_value()) {
            return true;
        }
        print_layout_diagnostics();
        return false;
    };

    if (command_line.language == Language::C || command_line.cpp_c_storage) {
        backend_c::CodegenOptions c_options;
        c_options.cpp_wrapper = command_line.cpp_c_storage;
        if (c_options.cpp_wrapper) {
            c_options.header_extension = ".generated.hpp";
            c_options.source_extension = ".generated.cpp";
        }
        c_options.optimize_size =
            command_line.c_optimize_size && command_line.c_optimize_size_namespaces.empty();
        c_options.optimize_size_namespaces = command_line.c_optimize_size_namespaces;
        c_options.name_prefix = command_line.codegen_options.name_prefix;
        c_options.output_directory = command_line.codegen_options.output_directory;
        c_options.root_file_stem = command_line.codegen_options.root_file_stem;

        const backend_c::Backend backend;
        if (command_line.list_outputs) {
            const backend_c::PlanResult plan_result = plan(backend, c_options);
            if (!plan_result.success) {
                errors << "backend error: " << plan_result.error_message << '\n';
                return exit_failure;
            }

            for (const backend_c::PlannedGeneratedFile& file : plan_result.plan.files) {
                output << backend_c::output_path_for_planned_file(c_options,
                                                                   file.relative_header_path)
                       << '\n';
                output << backend_c::output_path_for_planned_file(c_options,
                                                                   file.relative_source_path)
                       << '\n';
            }
            return exit_success;
        }

        const backend_c::CodegenResult codegen_result = generate(backend, c_options);
        if (!codegen_result.success) {
            errors << "backend error: " << codegen_result.error_message << '\n';
            return exit_failure;
        }
        if (!require_prepared() ||
            !write_generated_files(codegen_result, c_options.output_directory, errors)) {
            return exit_failure;
        }

        return exit_success;
    }

    if (command_line.language == Language::Go) {
        backend_go::CodegenOptions go_options;
        go_options.name_prefix = command_line.codegen_options.name_prefix;
        go_options.output_directory = command_line.codegen_options.output_directory;
        go_options.root_file_stem = command_line.codegen_options.root_file_stem;
        go_options.go_import_prefix = command_line.go_import_prefix;

        const backend_go::Backend backend;
        if (command_line.list_outputs) {
            const backend_go::PlanResult plan_result = plan(backend, go_options);
            if (!plan_result.success) {
                errors << "backend error: " << plan_result.error_message << '\n';
                return exit_failure;
            }
            for (const backend_go::PlannedGeneratedFile& file : plan_result.plan.files) {
                output << backend_go::output_path_for_planned_file(go_options,
                                                                    file.relative_output_path)
                       << '\n';
            }
            return exit_success;
        }

        const backend_go::CodegenResult codegen_result = generate(backend, go_options);
        if (!codegen_result.success) {
            errors << "backend error: " << codegen_result.error_message << '\n';
            return exit_failure;
        }
        if (!require_prepared() ||
            !write_generated_files(codegen_result, go_options.output_directory, errors)) {
            return exit_failure;
        }
        return exit_success;
    }

    if (command_line.language == Language::Python) {
        backend_python::CodegenOptions python_options;
        python_options.name_prefix = command_line.codegen_options.name_prefix;
        python_options.output_directory = command_line.codegen_options.output_directory;
        python_options.root_module_stem = command_line.codegen_options.root_file_stem;

        const backend_python::Backend backend;
        if (command_line.list_outputs) {
            const backend_python::PlanResult plan_result = plan(backend, python_options);
            if (!plan_result.success) {
                errors << "backend error: " << plan_result.error_message << '\n';
                return exit_failure;
            }

            for (const backend_python::PlannedGeneratedFile& file : plan_result.plan.files) {
                output << backend_python::output_path_for_planned_file(
                              python_options, file.relative_output_path)
                       << '\n';
            }
            return exit_success;
        }

        const backend_python::CodegenResult codegen_result = generate(backend, python_options);
        if (!codegen_result.success) {
            errors << "backend error: " << codegen_result.error_message << '\n';
            return exit_failure;
        }
        if (!require_prepared() ||
            !write_generated_files(codegen_result, python_options.output_directory, errors)) {
            return exit_failure;
        }

        return exit_success;
    }

    const backend::CodegenOptions& cpp_options = command_line.codegen_options;
    const backend::Backend backend;
    if (command_line.list_outputs) {
        const backend::PlanResult plan_result = plan(backend, cpp_options);
        if (!plan_result.success) {
            errors << "backend error: " << plan_result.error_message << '\n';
            return exit_failure;
        }

        for (const backend::PlannedGeneratedFile& file : plan_result.plan.files) {
            output << backend::output_path_for_planned_file(cpp_options, file.relative_output_path)
                   << '\n';
        }
        return exit_success;
    }

    const backend::CodegenResult codegen_result = generate(backend, cpp_options);
    if (!codegen_result.success) {
        errors << "backend error: " << codegen_result.error_message << '\n';
        return exit_failure;
    }
    if (!require_prepared() ||
        !write_generated_files(codegen_result, cpp_options.output_directory, errors)) {
        return exit_failure;
    }

    return exit_success;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::optional<CommandLine> command_line =
            parse_command_line(argc, argv, std::cerr);
        if (!command_line.has_value()) {
            print_usage(std::cerr);
            return exit_usage;
        }
        return run_schema_compiler(*command_line, std::cout, std::cerr);
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return exit_failure;
    }
}
