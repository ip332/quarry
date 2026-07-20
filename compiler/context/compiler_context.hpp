#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/file_system.hpp"
#include "compiler/support/source_manager.hpp"

#include <memory>

namespace quarry::compiler::context {

class CompilerContext {
public:
    CompilerContext();
    explicit CompilerContext(std::unique_ptr<support::FileSystem> file_system);
    ~CompilerContext();

    CompilerContext(const CompilerContext&) = delete;
    CompilerContext& operator=(const CompilerContext&) = delete;
    CompilerContext(CompilerContext&&) noexcept;
    CompilerContext& operator=(CompilerContext&&) noexcept;

    [[nodiscard]] support::SourceManager& source_manager();
    [[nodiscard]] const support::SourceManager& source_manager() const;

    [[nodiscard]] support::FileSystem& file_system();
    [[nodiscard]] const support::FileSystem& file_system() const;

    [[nodiscard]] diagnostics::DiagnosticEngine& diagnostic_engine();
    [[nodiscard]] const diagnostics::DiagnosticEngine& diagnostic_engine() const;

private:
    support::SourceManager source_manager_;
    std::unique_ptr<support::FileSystem> file_system_;
    diagnostics::DiagnosticEngine diagnostic_engine_;
};

} // namespace quarry::compiler::context
