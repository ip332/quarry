#include "compiler/context/compiler_context.hpp"

#include <stdexcept>
#include <utility>

namespace breadcrumbs::compiler::context {

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

} // namespace breadcrumbs::compiler::context
