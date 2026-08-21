#include "pdf.h"

#include <limits>

namespace mecaps::pdf {

namespace {

[[nodiscard]] bool exceedsPageExtent(double origin, double extent, double pageExtent) noexcept
{
	constexpr double toleranceFactor = 1.0 + 4.0 * std::numeric_limits<double>::epsilon();
	return origin + extent > pageExtent * toleranceFactor;
}

} // namespace

DocumentError validateRenderRequest(const PageMetadata &page, const RenderRequest &request, const PixelBuffer &buffer) noexcept
{
	const auto &clip = request.clip;
	if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.width)
	    || !std::isfinite(clip.height) || clip.x < 0.0 || clip.y < 0.0 || clip.width <= 0.0
	    || clip.height <= 0.0 || exceedsPageExtent(clip.x, clip.width, page.widthPoints)
	    || exceedsPageExtent(clip.y, clip.height, page.heightPoints) || request.outputWidth == 0 || request.outputHeight == 0
	    || buffer.width != request.outputWidth || buffer.height != request.outputHeight) {
		return DocumentError::invalidRenderRequest;
	}

	const auto pixelSize = bytesPerPixel(buffer.format);
	const auto minimumStride = static_cast<std::size_t>(buffer.width) * pixelSize;
	if (pixelSize == 0 || buffer.strideBytes < minimumStride) {
		return DocumentError::invalidRenderRequest;
	}

	const auto requiredBytes = static_cast<std::size_t>(buffer.strideBytes) * buffer.height;
	return requiredBytes <= buffer.pixels.size() ? DocumentError::none : DocumentError::invalidRenderRequest;
}

} // namespace mecaps::pdf
