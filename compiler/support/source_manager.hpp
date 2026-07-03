#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace breadcrumbs::compiler::support {

struct SourceUnit {
    std::string path;
    std::string text;
};

class SourceManager {
public:
    void add_source(std::string path, std::string text);
    [[nodiscard]] const std::vector<SourceUnit>& sources() const;

private:
    std::vector<SourceUnit> sources_;
};

}  // namespace breadcrumbs::compiler::support
