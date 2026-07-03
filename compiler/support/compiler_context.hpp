#pragma once

#include "compiler/support/file_system.hpp"
#include "compiler/support/source_manager.hpp"

namespace breadcrumbs::compiler::support {

class CompilerContext {
public:
    [[nodiscard]] SourceManager& source_manager();
    [[nodiscard]] const SourceManager& source_manager() const;

    [[nodiscard]] const FileSystem& file_system() const;

private:
    SourceManager source_manager_;
    FileSystem file_system_;
};

}  // namespace breadcrumbs::compiler::support
