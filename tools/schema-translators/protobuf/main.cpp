#include "descriptor_model.hpp"
#include "translation.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_help() {
    std::cout << "quarry-protobuf-translator [options]\n\n"
                 "Options:\n"
                 "  --descriptor-set PATH  protobuf FileDescriptorSet input\n"
                 "  --list                 list descriptor contents\n"
                 "  --root FQN             protobuf message root for translation\n"
                 "  --bounds PATH          YAML bounds file for translation\n"
                 "  --output-dir PATH      output directory for translated BRD files\n"
                 "  --help                 show this help\n\n"
                 "Listing is deterministic. Translation emits BRD and manifest.json.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string descriptor_set;
    std::string root;
    std::string bounds;
    std::string output_directory;
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
        if (root.empty() || bounds.empty() || output_directory.empty()) {
            std::cerr << "quarry-protobuf-translator: error: translation requires --root, --bounds, and "
                         "--output-dir\n";
            return 2;
        }
    } else if (!root.empty() || !bounds.empty() || !output_directory.empty()) {
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
    const quarry::tools::protobuf::TranslationResult translation =
        quarry::tools::protobuf::translate_descriptor_model(*result.model, root, bounds,
                                                            output_directory);
    if (!translation.succeeded()) {
        for (const std::string& diagnostic : translation.diagnostics) {
            std::cerr << "quarry-protobuf-translator: error: " << diagnostic << "\n";
        }
        return 1;
    }
    return 0;
}
