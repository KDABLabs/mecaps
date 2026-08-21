#pragma once

#include "pdf.h"

namespace mecaps::pdf {

[[nodiscard]] std::unique_ptr<Backend> createPdfiumBackend();

} // namespace mecaps::pdf
