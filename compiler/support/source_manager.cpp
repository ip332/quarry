#include "compiler/support/source_manager.hpp"

#include <utility>

namespace breadcrumbs::compiler::support {

void SourceManager::add_source(std::string path, std::string text) {
    sources_.push_back(SourceUnit{std::move(path), std::move(text)});
}

const std::vector<SourceUnit>& SourceManager::sources() const {
    return sources_;
}

}  // namespace breadcrumbs::compiler::support
