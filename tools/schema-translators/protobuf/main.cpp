#include "descriptor_model.hpp"
#include "translation.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::cout << "quarry-protobuf-translator [options]\n\n"
                 "Options:\n"
                 "  --descriptor-set PATH  protobuf FileDescriptorSet input\n"
                 "  --list                 list descriptor contents\n"
                 "  --root FQN             protobuf message root for translation\n"
                 "  --options PATH         Quarry-native YAML bounds/options file\n"
                 "  --options-format FMT   quarry (default) or nanopb\n"
                 "  --bounds PATH          compatibility alias for --options\n"
                 "  --output-dir PATH      output directory for translated BRD files\n"
                 "  --help                 show this help\n\n"
                 "Listing is deterministic. Translation emits BRD and manifest.json.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string descriptor_set;
    std::string root;
    std::string bounds;
    std::vector<std::string> options;
    std::string output_directory;
    std::string options_format = "quarry";
    bool options_format_set = false;
    bool list = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help") {
            print_help();
            return 0;
        }
        if (argument == "--list") {
            list = true;
            continue;
        }
        if (argument == "--descriptor-set" || argument == "--root" || argument == "--bounds" ||
            argument == "--options" || argument == "--options-format" ||
            argument == "--output-dir") {
            if (index + 1 >= argc) {
                std::cerr << "quarry-protobuf-translator: error: " << argument
                          << " requires a value\n";
                return 2;
            }
            const std::string value = argv[++index];
            if (value.empty()) {
                std::cerr << "quarry-protobuf-translator: error: " << argument
                          << " requires a non-empty value\n";
                return 2;
            }
            if (argument == "--descriptor-set") descriptor_set = value;
            if (argument == "--root") root = value;
            if (argument == "--bounds") bounds = value;
            if (argument == "--options") options.push_back(value);
            if (argument == "--options-format") {
                if (options_format_set) {
                    std::cerr << "quarry-protobuf-translator: error: --options-format may be supplied only once\n";
                    return 2;
                }
                options_format = value;
                options_format_set = true;
            }
            if (argument == "--output-dir") output_directory = value;
            continue;
        }
        std::cerr << "quarry-protobuf-translator: error: unknown option '" << argument << "'\n";
        return 2;
    }

    if (descriptor_set.empty()) {
        std::cerr << "quarry-protobuf-translator: error: --descriptor-set is required\n";
        return 2;
    }
    if (!list) {
        if (root.empty() || (bounds.empty() && options.empty()) || output_directory.empty()) {
            std::cerr << "quarry-protobuf-translator: error: translation requires --root, --options (or --bounds), and "
                         "--output-dir\n";
            return 2;
        }
        if (!bounds.empty() && !options.empty()) {
            std::cerr << "quarry-protobuf-translator: error: --options and --bounds cannot be combined\n";
            return 2;
        }
        if (options_format != "quarry" && options_format != "nanopb") {
            std::cerr << "quarry-protobuf-translator: error: unsupported --options-format '"
                      << options_format << "' (expected quarry or nanopb)\n";
            return 2;
        }
        if (options_format == "nanopb" && !bounds.empty()) {
            std::cerr << "quarry-protobuf-translator: error: --bounds is a Quarry-native alias and cannot be used with nanopb format\n";
            return 2;
        }
    } else if (!root.empty() || !bounds.empty() || !options.empty() || !output_directory.empty() ||
               options_format_set) {
        std::cerr << "quarry-protobuf-translator: error: --list cannot be combined with translation options\n";
        return 2;
    }

    const quarry::tools::protobuf::DescriptorLoadResult result =
        quarry::tools::protobuf::load_descriptor_set(descriptor_set);
    if (!result.succeeded()) {
        for (const std::string& diagnostic : result.diagnostics) {
            std::cerr << "quarry-protobuf-translator: error: " << diagnostic << "\n";
        }
        return 1;
    }

    if (list) {
        std::cout << quarry::tools::protobuf::render_descriptor_list(*result.model);
        return 0;
    }
    quarry::tools::protobuf::OptionsFormat format =
        options_format == "nanopb" ? quarry::tools::protobuf::OptionsFormat::Nanopb
                                    : quarry::tools::protobuf::OptionsFormat::Quarry;
    if (!bounds.empty()) options.push_back(bounds);
    const quarry::tools::protobuf::TranslationResult translation =
        quarry::tools::protobuf::translate_descriptor_model(*result.model, root, options, format,
                                                            output_directory);
    if (!translation.succeeded()) {
        for (const std::string& diagnostic : translation.diagnostics) {
            std::cerr << "quarry-protobuf-translator: error: " << diagnostic << "\n";
        }
        return 1;
    }
    return 0;
}
