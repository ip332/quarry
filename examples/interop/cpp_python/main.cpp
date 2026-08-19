#include "demo/interop.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--decode") {
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) {
            return 1;
        }
        input.seekg(0, std::ios::end);
        const auto size = input.tellg();
        if (size < 0) {
            return 1;
        }
        input.seekg(0, std::ios::beg);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return 1;
        }
        const auto decoded = demo::interop::decode_Sample(bytes);
        if (!decoded.has_value() || !decoded->has_count() || decoded->count() == nullptr ||
            *decoded->count() != 42U || !decoded->has_label() || decoded->label() == nullptr ||
            *decoded->label() != "hello") {
            return 2;
        }
        std::cout << "decoded count: " << *decoded->count() << "\n";
        return 0;
    }

    const std::string output_path = argc == 2 ? argv[1] : "encoded.brf";

    demo::interop::SampleBuilder builder;
    if (!builder.set_count(42U) || !builder.set_label("hello")) {
        return 1;
    }
    const auto encoded = demo::interop::encode(builder.build());
    if (!encoded.has_value()) {
        return 2;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return 3;
    }
    output.write(reinterpret_cast<const char*>(encoded->data()),
                 static_cast<std::streamsize>(encoded->size()));
    if (!output) {
        return 4;
    }
    std::cout << "wrote " << encoded->size() << " bytes\n";
    return 0;
}
