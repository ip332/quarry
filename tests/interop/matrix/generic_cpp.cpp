// QBS tooling API consumer only: no IR/layout/identity calculation oracle.
#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::uint8_t> load(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("C++ generic interpretation disagrees with truck fixture");
    }
}
template <typename T> T checked(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::runtime_error("expected QBS/BRF view is absent");
    }
    return *value;
}
void timestamp(const quarry::runtime::ValidatedBrfRecordView& value, std::uint64_t seed) {
    require(value.schema().record_id == 0x00040001U);
    const auto field = checked(value.field(0U));
    require(field.as_unsigned() == 42U + seed);
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3);
        const auto qbs_bytes = load(argv[1]);
        const auto brf_bytes = load(argv[2]);
        quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
        const auto qbs = checked(quarry::compiler::qbs::parse_qbs(qbs_bytes, diagnostics));
        const auto record = checked(qbs.find_record_by_id(0x00050001U));
        const auto engine = checked(quarry::runtime::validate_brf_record(qbs, record, brf_bytes));
        const auto time = checked(engine.field(0U));
        timestamp(checked(time.as_record()), 0U);
        const auto rpm = checked(engine.field(1U));
        const auto coolant = checked(engine.field(2U));
        require(rpm.as_unsigned() == 1450U);
        require(coolant.as_signed() == -12);
        const auto faults = checked(engine.record_array(3U));
        require(faults.size() == 2U);
        for (std::size_t index = 0U; index < faults.size(); ++index) {
            const auto fault = checked(faults.element(index));
            require(fault.schema().record_id == 0x00050002U);
            const auto fault_time = checked(fault.field(0U));
            timestamp(checked(fault_time.as_record()), index);
            const auto spn = checked(fault.field(1U));
            const auto fmi = checked(fault.field(2U));
            require(spn.as_unsigned() == 1234U + index);
            require(fmi.as_unsigned() == 7U + index);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
