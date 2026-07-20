#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/yaml/yaml_document.hpp"

namespace quarry::compiler::yaml {

[[nodiscard]] source_schema::SourceSchemaDecodeResult
decode_schema(const YamlDocument& document, diagnostics::DiagnosticEngine& diagnostics);

} // namespace quarry::compiler::yaml
