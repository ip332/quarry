#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/support/file_system.hpp"
#include "compiler/support/source_manager.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quarry::compiler::context {

struct SourceUnitImport {
    std::string requested_path;
    std::string resolved_path;
    support::SourceRange source_range = support::SourceRange::invalid();
};

struct SourceUnit {
    std::string canonical_path;
    std::string identity;
    std::string namespace_fqn;
    support::SourceFileId source_file_id = support::SourceFileId::invalid();
    support::SourceRange source_range = support::SourceRange::invalid();
    bool is_root = false;
    std::vector<SourceUnitImport> imports;
    std::optional<source_schema::NormalizedSourceSchemaDocument> schema;
};

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

    [[nodiscard]] bool register_source_unit(SourceUnit source_unit);
    [[nodiscard]] const std::vector<SourceUnit>& source_units() const;
    [[nodiscard]] const SourceUnit* find_source_unit(std::string_view canonical_path) const;
    [[nodiscard]] const SourceUnit* find_source_unit_by_identity(
        std::string_view identity) const;
    void clear_source_units();

private:
    support::SourceManager source_manager_;
    std::unique_ptr<support::FileSystem> file_system_;
    diagnostics::DiagnosticEngine diagnostic_engine_;
    std::vector<SourceUnit> source_units_;
};

} // namespace quarry::compiler::context
