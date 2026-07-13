#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/yaml/yaml_document.hpp"
#include "compiler/yaml/source_schema.hpp"

namespace breadcrumbs::compiler::yaml {

[[nodiscard]] YamlDecodeResult decode_schema(const YamlDocument& document,
                                             diagnostics::DiagnosticEngine& diagnostics);

} // namespace breadcrumbs::compiler::yaml
