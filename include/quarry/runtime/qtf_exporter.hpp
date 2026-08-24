#pragma once

#include "quarry/runtime/qbs_brf_reader.hpp"

#include <optional>
#include <string>

namespace quarry::runtime {

enum class QtfExportError { none, traversal_limit, internal_error, invalid_value };

struct QtfExportResult {
    std::optional<std::string> text;
    QtfExportError error = QtfExportError::none;
};

[[nodiscard]] QtfExportResult export_qtf(const ValidatedBrfRecordView& record,
                                         BrfTraversalLimits limits = {});

} // namespace quarry::runtime
