#include "compiler/support/compiler_context.hpp"

namespace breadcrumbs::compiler::support {

SourceManager& CompilerContext::source_manager() {
    return source_manager_;
}

const SourceManager& CompilerContext::source_manager() const {
    return source_manager_;
}

const FileSystem& CompilerContext::file_system() const {
    return file_system_;
}

}  // namespace breadcrumbs::compiler::support
