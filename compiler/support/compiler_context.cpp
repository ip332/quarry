#include "compiler/support/compiler_context.hpp"

#include <stdexcept>
#include <utility>

namespace breadcrumbs::compiler::support {

CompilerContext::CompilerContext() : CompilerContext(std::make_unique<RealFileSystem>()) {}

CompilerContext::CompilerContext(std::unique_ptr<FileSystem> file_system)
    : file_system_(std::move(file_system)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("CompilerContext requires a filesystem");
    }
}

CompilerContext::~CompilerContext() = default;

CompilerContext::CompilerContext(CompilerContext&&) noexcept = default;

CompilerContext& CompilerContext::operator=(CompilerContext&&) noexcept = default;

SourceManager& CompilerContext::source_manager() { return source_manager_; }

const SourceManager& CompilerContext::source_manager() const { return source_manager_; }

FileSystem& CompilerContext::file_system() { return *file_system_; }

const FileSystem& CompilerContext::file_system() const { return *file_system_; }

} // namespace breadcrumbs::compiler::support
