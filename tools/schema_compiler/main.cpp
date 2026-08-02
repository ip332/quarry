#include "compiler/backend/backend.hpp"
#include "compiler/backend/generated_code_api_version.hpp"
#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_python/backend_python.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/support/source_location.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace backend = quarry::compiler::backend;
namespace backend_c = quarry::compiler::backend_c;
namespace backend_python = quarry::compiler::backend_python;
namespace context = quarry::compiler::context;
namespace diagnostics = quarry::compiler::diagnostics;
namespace frontend = quarry::compiler::frontend;

#ifndef QUARRY_VERSION
#define QUARRY_VERSION "0.0.0"
#endif

constexpr int exit_success = 0;
constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

enum class Language {
    Cpp,
    C,
    Python,
};

struct CommandLine {
    bool show_help = false;
    bool show_version = false;
    bool show_generated_code_api_version = false;
    bool list_outputs = false;
    Language language = Language::Cpp;
    std::string input_path;
    backend::CodegenOptions codegen_options;
};

[[nodiscard]] std::string usage_text() {
    return "usage: quarry-schema-compiler [options] INPUT\n"
           "\n"
           "Options:\n"
           "  -o, --output-directory PATH  Directory for generated files (default: generated)\n"
           "      --root-file-stem NAME     Root namespace file stem (default: schema)\n"
           "      --file-extension EXT      Generated file extension for --language cpp\n"
           "                                (default: .generated.hpp)\n"
           "      --language {cpp,c,python} Target backend language (default: cpp)\n"
           "      --list-outputs            Print generated output paths without writing files\n"
           "      --print-generated-code-api-version\n"
           "                                Print the generated-code API compatibility version and "
           "exit\n"
           "      --version                 Show version information\n"
           "  -h, --help                    Show this help text\n";
}

void print_usage(std::ostream& output) { output << usage_text(); }

void print_version(std::ostream& output) {
    output << "quarry-schema-compiler " << QUARRY_VERSION << '\n';
}

void print_generated_code_api_version(std::ostream& output) {
    output << quarry::compiler::backend::kGeneratedCodeApiVersion << '\n';
}

[[nodiscard]] bool option_requires_value(std::string_view option) {
    return option == "-o" || option == "--output-directory" || option == "--root-file-stem" ||
           option == "--file-extension" || option == "--language";
}

[[nodiscard]] std::optional<CommandLine> parse_command_line(int argc, char** argv,
                                                            std::ostream& errors) {
    CommandLine command_line;
    bool saw_output_directory = false;
    bool saw_root_file_stem = false;
    bool saw_file_extension = false;
    bool saw_language = false;

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
        if (argument == "--list-outputs") {
            command_line.list_outputs = true;
            continue;
        }

        if (option_requires_value(argument)) {
            if (index + 1 >= argc) {
                errors << "error: missing value for " << argument << '\n';
                return std::nullopt;
            }

            const std::string value = argv[++index];
            if (value.empty()) {
                errors << "error: empty value for " << argument << '\n';
                return std::nullopt;
            }

            if (argument == "-o" || argument == "--output-directory") {
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
                } else {
                    errors << "error: invalid value for --language: " << value
                           << " (expected 'cpp', 'c', or 'python')\n";
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

    if (command_line.show_generated_code_api_version) {
        const bool has_generation_arguments =
            command_line.list_outputs || !command_line.input_path.empty() ||
            command_line.codegen_options.output_directory != "generated" ||
            command_line.codegen_options.root_file_stem != "schema" ||
            command_line.codegen_options.file_extension != ".generated.hpp" ||
            command_line.language != Language::Cpp;
        if (has_generation_arguments) {
            errors << "error: --print-generated-code-api-version does not accept generation "
                      "options or an input file\n";
            return std::nullopt;
        }
    }

    if (command_line.language == Language::C && saw_file_extension) {
        errors << "error: --file-extension is not supported with --language c\n";
        return std::nullopt;
    }

    if (command_line.language == Language::Python && saw_file_extension) {
        errors << "error: --file-extension is not supported with --language python\n";
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
        compiler.compile(source_file_id, compiler_context, diagnostic_engine);

    if (!diagnostic_engine.empty()) {
        errors << diagnostics::DiagnosticFormatter::format_all(
            diagnostic_engine, compiler_context.source_manager());
        errors << '\n';
    }

    if (!compilation_result.succeeded()) {
        return exit_failure;
    }

    if (command_line.language == Language::C) {
        backend_c::CodegenOptions c_options;
        c_options.output_directory = command_line.codegen_options.output_directory;
        c_options.root_file_stem = command_line.codegen_options.root_file_stem;

        backend_c::Backend backend;
        if (command_line.list_outputs) {
            const backend_c::PlanResult plan_result =
                backend.plan(*compilation_result.schema_ir, c_options);
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

        const backend_c::CodegenResult codegen_result =
            backend.generate(*compilation_result.schema_ir, c_options);
        if (!codegen_result.success) {
            errors << "backend error: " << codegen_result.error_message << '\n';
            return exit_failure;
        }

        if (!write_generated_files(codegen_result, c_options.output_directory, errors)) {
            return exit_failure;
        }

        return exit_success;
    }

    if (command_line.language == Language::Python) {
        backend_python::CodegenOptions python_options;
        python_options.output_directory = command_line.codegen_options.output_directory;
        python_options.root_module_stem = command_line.codegen_options.root_file_stem;

        backend_python::Backend backend;
        if (command_line.list_outputs) {
            const backend_python::PlanResult plan_result =
                backend.plan(*compilation_result.schema_ir, python_options);
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

        const backend_python::CodegenResult codegen_result =
            backend.generate(*compilation_result.schema_ir, python_options);
        if (!codegen_result.success) {
            errors << "backend error: " << codegen_result.error_message << '\n';
            return exit_failure;
        }

        if (!write_generated_files(codegen_result, python_options.output_directory, errors)) {
            return exit_failure;
        }

        return exit_success;
    }

    backend::Backend backend;
    if (command_line.list_outputs) {
            const backend::PlanResult plan_result =
                backend.plan(*compilation_result.schema_ir, command_line.codegen_options,
                             &*compilation_result.output_plan);
        if (!plan_result.success) {
            errors << "backend error: " << plan_result.error_message << '\n';
            return exit_failure;
        }

        for (const backend::PlannedGeneratedFile& file : plan_result.plan.files) {
            output << backend::output_path_for_planned_file(
                          command_line.codegen_options, file.relative_output_path)
                   << '\n';
        }
        return exit_success;
    }

    const backend::CodegenResult codegen_result =
        backend.generate(*compilation_result.schema_ir, command_line.codegen_options,
                         &*compilation_result.output_plan);
    if (!codegen_result.success) {
        errors << "backend error: " << codegen_result.error_message << '\n';
        return exit_failure;
    }

    if (!write_generated_files(codegen_result, command_line.codegen_options.output_directory,
                               errors)) {
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
