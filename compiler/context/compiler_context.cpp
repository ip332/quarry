#include "compiler/context/compiler_context.hpp"

#include <stdexcept>
#include <algorithm>
#include <utility>

namespace quarry::compiler::context {

CompilerContext::CompilerContext() : CompilerContext(std::make_unique<support::RealFileSystem>()) {}

CompilerContext::CompilerContext(std::unique_ptr<support::FileSystem> file_system)
    : file_system_(std::move(file_system)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("CompilerContext requires a filesystem");
    }
}

CompilerContext::~CompilerContext() = default;

CompilerContext::CompilerContext(CompilerContext&&) noexcept = default;

CompilerContext& CompilerContext::operator=(CompilerContext&&) noexcept = default;

support::SourceManager& CompilerContext::source_manager() { return source_manager_; }

const support::SourceManager& CompilerContext::source_manager() const { return source_manager_; }

support::FileSystem& CompilerContext::file_system() { return *file_system_; }

const support::FileSystem& CompilerContext::file_system() const { return *file_system_; }

diagnostics::DiagnosticEngine& CompilerContext::diagnostic_engine() { return diagnostic_engine_; }

const diagnostics::DiagnosticEngine& CompilerContext::diagnostic_engine() const {
    return diagnostic_engine_;
}

bool CompilerContext::register_source_unit(SourceUnit source_unit) {
    const auto existing = std::find_if(
        source_units_.begin(), source_units_.end(), [&](const SourceUnit& unit) {
            return unit.canonical_path == source_unit.canonical_path;
        });
    if (existing != source_units_.end()) {
        return false;
    }
    source_units_.push_back(std::move(source_unit));
    return true;
}

const std::vector<SourceUnit>& CompilerContext::source_units() const { return source_units_; }

const SourceUnit* CompilerContext::find_source_unit(std::string_view canonical_path) const {
    const auto found = std::find_if(
        source_units_.begin(), source_units_.end(), [&](const SourceUnit& unit) {
            return unit.canonical_path == canonical_path;
        });
    return found == source_units_.end() ? nullptr : &*found;
}

const SourceUnit* CompilerContext::find_source_unit_by_identity(std::string_view identity) const {
    const auto found = std::find_if(source_units_.begin(), source_units_.end(),
                                    [&](const SourceUnit& unit) {
                                        return unit.identity == identity;
                                    });
    return found == source_units_.end() ? nullptr : &*found;
}

void CompilerContext::clear_source_units() { source_units_.clear(); }

} // namespace quarry::compiler::context
