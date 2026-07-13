#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/yaml/yaml_document.hpp"

#include <optional>

namespace breadcrumbs::compiler::yaml {

struct YamlParseResult {
    std::optional<YamlDocument> document;
};

class YamlParser {
public:
    [[nodiscard]] static YamlParseResult
    parse(const support::SourceManager& source_manager, support::SourceFileId source_file_id,
          diagnostics::DiagnosticEngine& diagnostics);
};

} // namespace breadcrumbs::compiler::yaml
