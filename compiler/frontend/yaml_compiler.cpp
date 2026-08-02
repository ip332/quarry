#include "compiler/frontend/yaml_compiler.hpp"

#include "compiler/layout/layout.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/symbols/symbols.hpp"
#include "compiler/yaml/schema_decoder.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <iterator>
#include <utility>
#include <vector>

namespace quarry::compiler::frontend {
namespace {

[[nodiscard]] bool has_fatal_diagnostics(const diagnostics::DiagnosticCollection& diagnostics) {
    return diagnostics.has_errors();
}

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_graph_diagnostic(diagnostics::DiagnosticCollection& diagnostics, std::string_view id,
                           std::string message, support::SourceRange range) {
    auto builder = diagnostics::Diagnostic::create(diagnostic_id(id), diagnostics::Severity::Error,
                                                   std::move(message))
                       .from_pass("source-unit-graph");
    if (range.is_valid()) {
        builder.at(range);
    }
    diagnostics.emit(builder.build());
}

[[nodiscard]] std::optional<source_schema::NormalizedSourceSchemaDocument>
decode_and_normalize(support::SourceFileId source_file_id, context::CompilerContext& context,
                     diagnostics::DiagnosticCollection& diagnostics) {
    const yaml::YamlParseResult parse_result =
        yaml::YamlParser::parse(context.source_manager(), source_file_id, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !parse_result.document.has_value()) {
        return std::nullopt;
    }

    const source_schema::SourceSchemaDecodeResult decode_result =
        yaml::decode_schema(*parse_result.document, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !decode_result.schema.has_value()) {
        return std::nullopt;
    }

    const source_schema::SourceSchemaNormalizationResult normalization_result =
        source_schema::normalize_source_schema(*decode_result.schema, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !normalization_result.document.has_value()) {
        return std::nullopt;
    }
    return normalization_result.document;
}

class SourceUnitGraphLoader {
public:
    SourceUnitGraphLoader(support::SourceFileId root_source_file_id,
                          context::CompilerContext& context,
                          diagnostics::DiagnosticCollection& diagnostics)
        : root_source_file_id_(root_source_file_id), context_(context), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<source_schema::NormalizedSourceSchemaDocument> load() {
        context_.clear_source_units();
        const std::optional<std::string_view> source_path =
            context_.source_manager().source_path(root_source_file_id_);
        if (!source_path.has_value()) {
            emit_graph_diagnostic(diagnostics_, "BC2407", "root source file is not registered",
                                  support::SourceRange::invalid());
            return std::nullopt;
        }

        const std::string canonical_path = context_.file_system().normalize_path(*source_path);
        return load_unit(canonical_path, root_source_file_id_, true);
    }

private:
    enum class VisitState { Visiting, Loaded };

    [[nodiscard]] std::optional<source_schema::NormalizedSourceSchemaDocument>
    load_unit(const std::string& canonical_path, support::SourceFileId source_file_id,
              bool is_root,
              support::SourceRange incoming_import_range = support::SourceRange::invalid()) {
        const auto existing = states_.find(canonical_path);
        if (existing != states_.end()) {
            if (existing->second == VisitState::Visiting) {
                emit_cycle(canonical_path, incoming_import_range);
                return std::nullopt;
            }
            if (is_root && root_document_.has_value()) {
                return root_document_;
            }
            return std::nullopt;
        }

        states_.emplace(canonical_path, VisitState::Visiting);
        stack_paths_.push_back(canonical_path);
        stack_import_ranges_.push_back(incoming_import_range);

        const std::optional<source_schema::NormalizedSourceSchemaDocument> document =
            decode_and_normalize(source_file_id, context_, diagnostics_);
        if (!document.has_value()) {
            states_.erase(canonical_path);
            stack_paths_.pop_back();
            stack_import_ranges_.pop_back();
            return std::nullopt;
        }

        context::SourceUnit source_unit;
        source_unit.canonical_path = canonical_path;
        source_unit.identity = document->namespace_name.text() + "." +
                               document->record_name.text;
        source_unit.namespace_fqn = document->namespace_name.text();
        source_unit.source_file_id = source_file_id;
        source_unit.source_range = document->source_range;
        source_unit.is_root = is_root;
        source_unit.schema = *document;

        const auto previous_identity = identities_.find(source_unit.identity);
        if (previous_identity != identities_.end() &&
            previous_identity->second != canonical_path) {
            auto builder = diagnostics::Diagnostic::create(
                               diagnostic_id("BC2406"), diagnostics::Severity::Error,
                               "duplicate source-unit identity '" + source_unit.identity +
                                   "' declared by '" + canonical_path + "'")
                               .from_pass("source-unit-graph");
            if (document->source_range.is_valid()) {
                builder.at(document->source_range);
            }
            const auto previous_range = identity_ranges_.find(source_unit.identity);
            if (previous_range != identity_ranges_.end() &&
                previous_range->second.is_valid()) {
                builder.with_related(diagnostics::RelatedLocation::at_range(
                    previous_range->second, "previous source-unit declaration is here"));
            }
            diagnostics_.emit(builder.build());
            states_.erase(canonical_path);
            stack_paths_.pop_back();
            stack_import_ranges_.pop_back();
            return std::nullopt;
        }
        identities_.emplace(source_unit.identity, canonical_path);
        identity_ranges_.emplace(source_unit.identity, document->source_range);

        if (document->imports.has_value()) {
            for (const source_schema::NormalizedSourceSchemaImports::Import& import :
                 document->imports->entries) {
                const std::string resolved_path = resolve_import_path(canonical_path, import.path);
                source_unit.imports.push_back(context::SourceUnitImport{
                    .requested_path = import.path,
                    .resolved_path = resolved_path,
                    .source_range = import.source_range,
                });

                if (!load_import(resolved_path, import.source_range)) {
                    states_.erase(canonical_path);
                    stack_paths_.pop_back();
                    stack_import_ranges_.pop_back();
                    return std::nullopt;
                }
            }
        }

        if (!context_.register_source_unit(std::move(source_unit))) {
            emit_graph_diagnostic(diagnostics_, "BC2406",
                                  "source unit '" + canonical_path + "' was loaded more than once",
                                  document->source_range);
            states_.erase(canonical_path);
            stack_paths_.pop_back();
            stack_import_ranges_.pop_back();
            return std::nullopt;
        }

        states_[canonical_path] = VisitState::Loaded;
        stack_paths_.pop_back();
        stack_import_ranges_.pop_back();
        if (is_root) {
            root_document_ = document;
        }
        return is_root ? document : std::nullopt;
    }

    [[nodiscard]] bool load_import(const std::string& canonical_path,
                                   support::SourceRange import_range) {
        const auto existing = states_.find(canonical_path);
        if (existing != states_.end()) {
            if (existing->second == VisitState::Visiting) {
                emit_cycle(canonical_path, import_range);
                return false;
            }
            return true;
        }

        const auto read_result = context_.file_system().read_text_file(canonical_path);
        if (!read_result.found) {
            emit_graph_diagnostic(diagnostics_, "BC2404",
                                  "unable to read imported source unit '" + canonical_path + "'",
                                  import_range);
            return false;
        }

        const support::SourceFileId source_file_id =
            context_.source_manager().add_source(canonical_path, read_result.text);
        (void)load_unit(canonical_path, source_file_id, false, import_range);
        return states_.contains(canonical_path) &&
               states_.at(canonical_path) == VisitState::Loaded;
    }

    [[nodiscard]] std::string resolve_import_path(std::string_view importing_path,
                                                  std::string_view requested_path) const {
        const std::filesystem::path base =
            std::filesystem::path(std::string(importing_path)).parent_path();
        return context_.file_system().normalize_path(
            (base / std::string(requested_path)).string());
    }

    void emit_cycle(std::string_view repeated_path, support::SourceRange import_range) {
        const auto first = std::find(stack_paths_.begin(), stack_paths_.end(), repeated_path);
        std::ostringstream path;
        if (first != stack_paths_.end()) {
            for (auto it = first; it != stack_paths_.end(); ++it) {
                if (it != first) {
                    path << " -> ";
                }
                path << *it;
            }
            path << " -> " << repeated_path;
        } else {
            path << repeated_path << " -> " << repeated_path;
        }
        auto builder = diagnostics::Diagnostic::create(
                           diagnostic_id("BC2405"), diagnostics::Severity::Error,
                           "import cycle detected: " + path.str())
                           .from_pass("source-unit-graph");
        if (import_range.is_valid()) {
            builder.at(import_range);
        }
        if (first != stack_paths_.end()) {
            const std::size_t first_index =
                static_cast<std::size_t>(std::distance(stack_paths_.begin(), first));
            for (std::size_t index = first_index + 1; index < stack_import_ranges_.size();
                 ++index) {
                if (stack_import_ranges_[index].is_valid()) {
                    builder.with_related(diagnostics::RelatedLocation::at_range(
                        stack_import_ranges_[index], "import edge in cycle"));
                }
            }
        }
        diagnostics_.emit(builder.build());
    }

    support::SourceFileId root_source_file_id_;
    context::CompilerContext& context_;
    diagnostics::DiagnosticCollection& diagnostics_;
    std::map<std::string, VisitState> states_;
    std::map<std::string, std::string> identities_;
    std::map<std::string, support::SourceRange> identity_ranges_;
    std::vector<std::string> stack_paths_;
    std::vector<support::SourceRange> stack_import_ranges_;
    std::optional<source_schema::NormalizedSourceSchemaDocument> root_document_;
};

} // namespace

YamlCompilationResult YamlCompiler::compile(support::SourceFileId source_file_id,
                                            context::CompilerContext& context,
                                            diagnostics::DiagnosticCollection& diagnostics) const {
    YamlCompilationResult result;

    SourceUnitGraphLoader graph_loader(source_file_id, context, diagnostics);
    const std::optional<source_schema::NormalizedSourceSchemaDocument> normalization_result =
        graph_loader.load();
    if (!normalization_result.has_value() || has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    std::vector<const source_schema::NormalizedSourceSchemaDocument*> schemas;
    schemas.reserve(context.source_units().size());
    for (const context::SourceUnit& source_unit : context.source_units()) {
        if (source_unit.schema.has_value()) {
            schemas.push_back(&*source_unit.schema);
        }
    }

    symbols::NamespaceBuilder namespace_builder;
    const symbols::SymbolTable symbol_table = namespace_builder.build(schemas, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    semantic::SemanticValidator semantic_validator;
    const semantic::SemanticModel semantic_model =
        semantic_validator.validate(schemas, symbol_table, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    layout::LayoutComputer layout_computer;
    const layout::LayoutModel layout_model =
        layout_computer.compute(semantic_model, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    schema_ir::SchemaIrBuilder schema_ir_builder;
    const schema_ir::SchemaIrModel schema_ir = schema_ir_builder.build(
        *normalization_result, semantic_model, layout_model, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    schema_ir::SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(schema_ir, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    result.schema_ir = std::move(schema_ir);
    return result;
}

} // namespace quarry::compiler::frontend
