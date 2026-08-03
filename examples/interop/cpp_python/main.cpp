#include "demo/interop.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
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
