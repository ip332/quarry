#pragma once

#include "compiler/support/file_system.hpp"
#include "compiler/support/source_manager.hpp"

#include <memory>

namespace breadcrumbs::compiler::support {

class CompilerContext {
public:
    CompilerContext();
    explicit CompilerContext(std::unique_ptr<FileSystem> file_system);
    ~CompilerContext();

    CompilerContext(const CompilerContext&) = delete;
    CompilerContext& operator=(const CompilerContext&) = delete;
    CompilerContext(CompilerContext&&) noexcept;
    CompilerContext& operator=(CompilerContext&&) noexcept;

    [[nodiscard]] SourceManager& source_manager();
    [[nodiscard]] const SourceManager& source_manager() const;

    [[nodiscard]] FileSystem& file_system();
    [[nodiscard]] const FileSystem& file_system() const;

private:
    SourceManager source_manager_;
    std::unique_ptr<FileSystem> file_system_;
};

} // namespace breadcrumbs::compiler::support
