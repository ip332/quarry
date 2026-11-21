// QBS tooling API consumer only: no IR/layout/identity calculation oracle.
#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
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
void timestamp(const quarry::runtime::ValidatedBrfRecordView& value, std::uint64_t seed) {
    require(value.schema().record_id == 0x00040001U);
    const auto field = value.field(0U);
    require(field && field->as_unsigned() == 42U + seed);
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3);
        const auto qbs_bytes = load(argv[1]);
        const auto brf_bytes = load(argv[2]);
        quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
        const auto qbs = quarry::compiler::qbs::parse_qbs(qbs_bytes, diagnostics);
        require(qbs.has_value());
        const auto record = qbs.value().find_record_by_id(0x00050001U);
        require(record.has_value());
        const auto engine =
            quarry::runtime::validate_brf_record(qbs.value(), record.value(), brf_bytes);
        require(engine.has_value());
        const auto time = engine.value().field(0U);
        require(time && time.value().as_record().has_value());
        timestamp(time.value().as_record().value(), 0U);
        const auto rpm = engine.value().field(1U);
        const auto coolant = engine.value().field(2U);
        require(rpm && rpm->as_unsigned() == 1450U);
        require(coolant && coolant->as_signed() == -12);
        const auto faults = engine.value().record_array(3U);
        require(faults && faults.value().size() == 2U);
        for (std::size_t index = 0U; index < faults.value().size(); ++index) {
            const auto fault = faults.value().element(index);
            require(fault.has_value() && fault.value().schema().record_id == 0x00050002U);
            const auto fault_time = fault.value().field(0U);
            require(fault_time && fault_time.value().as_record().has_value());
            timestamp(fault_time.value().as_record().value(), index);
            const auto spn = fault.value().field(1U);
            const auto fmi = fault.value().field(2U);
            require(spn && spn->as_unsigned() == 1234U + index);
            require(fmi && fmi->as_unsigned() == 7U + index);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
