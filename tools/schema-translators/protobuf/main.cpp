#include "descriptor_model.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_help() {
    std::cout << "quarry-protobuf-translator [options]\n\n"
                 "Options:\n"
                 "  --descriptor-set PATH  protobuf FileDescriptorSet input\n"
                 "  --list                 list descriptor contents\n"
                 "  --help                 show this help\n\n"
                 "BRD generation is not implemented yet.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string descriptor_set;
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
        if (argument == "--descriptor-set") {
            if (index + 1 >= argc) {
                std::cerr << "quarry-protobuf-translator: error: --descriptor-set requires PATH\n";
                return 2;
            }
            descriptor_set = argv[++index];
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
        std::cerr << "quarry-protobuf-translator: error: no inspection mode selected; use --list\n";
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

    std::cout << quarry::tools::protobuf::render_descriptor_list(*result.model);
    return 0;
}
