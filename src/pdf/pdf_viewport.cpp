#include "pdf_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mecaps::pdf {

namespace {

constexpr double minimumScale = 0.01;
constexpr double maximumScale = 64.0;

bool positiveFinite(double value)
{
	return std::isfinite(value) && value > 0.0;
}

} // namespace

Viewport::Viewport(std::size_t maxOutputPixels)
    : m_maxOutputPixels(std::max<std::size_t>(1, maxOutputPixels))
{
}

void Viewport::setPage(std::size_t pageIndex, PageMetadata page)
{
	m_pageIndex = pageIndex;
	m_page = page;
	m_center = { page.widthPoints / 2.0, page.heightPoints / 2.0 };
	updateFitScale();
}

void Viewport::setViewport(double width, double height, double displayScale)
{
	m_viewportWidth = positiveFinite(width) ? width : 0.0;
	m_viewportHeight = positiveFinite(height) ? height : 0.0;
	m_displayScale = positiveFinite(displayScale) ? displayScale : 1.0;
	updateFitScale();
}

void Viewport::setFitMode(FitMode mode)
{
	m_fitMode = mode;
	updateFitScale();
}

void Viewport::zoomBy(double factor, ViewportPoint anchor)
{
	if (!positiveFinite(factor) || !positiveFinite(m_scale) || m_viewportWidth <= 0.0 || m_viewportHeight <= 0.0)
		return;

	const ViewportPoint offset { anchor.x - m_viewportWidth / 2.0, anchor.y - m_viewportHeight / 2.0 };
	const ViewportPoint documentAnchor { m_center.x + offset.x / m_scale, m_center.y + offset.y / m_scale };
	m_scale = std::clamp(m_scale * factor, minimumScale, maximumScale);
	m_center = { documentAnchor.x - offset.x / m_scale, documentAnchor.y - offset.y / m_scale };
	m_fitMode = FitMode::custom;
	clampCenter();
}

bool Viewport::panBy(double deltaX, double deltaY)
{
	if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !positiveFinite(m_scale))
		return false;
	const auto previousCenter = m_center;
	m_center.x -= deltaX / m_scale;
	m_center.y -= deltaY / m_scale;
	clampCenter();
	return m_center.x != previousCenter.x || m_center.y != previousCenter.y;
}

PanBounds Viewport::panBounds() const noexcept
{
	if (!positiveFinite(m_page.widthPoints) || !positiveFinite(m_page.heightPoints)
	    || !positiveFinite(m_viewportWidth) || !positiveFinite(m_viewportHeight) || !positiveFinite(m_scale))
		return {};

	const double halfVisibleWidth = std::min(m_page.widthPoints, m_viewportWidth / m_scale) / 2.0;
	const double halfVisibleHeight = std::min(m_page.heightPoints, m_viewportHeight / m_scale) / 2.0;
	return {
		(m_center.x - (m_page.widthPoints - halfVisibleWidth)) * m_scale,
		(m_center.x - halfVisibleWidth) * m_scale,
		(m_center.y - (m_page.heightPoints - halfVisibleHeight)) * m_scale,
		(m_center.y - halfVisibleHeight) * m_scale,
	};
}

std::optional<ViewportRect> Viewport::placementFor(PageClip clip) const
{
	if (!positiveFinite(m_viewportWidth) || !positiveFinite(m_viewportHeight) || !positiveFinite(m_scale)
	    || !positiveFinite(clip.width) || !positiveFinite(clip.height))
		return std::nullopt;

	const double pageLeft = m_viewportWidth / 2.0 - m_center.x * m_scale;
	const double pageTop = m_viewportHeight / 2.0 - m_center.y * m_scale;
	return ViewportRect {
		pageLeft + clip.x * m_scale,
		pageTop + clip.y * m_scale,
		clip.width * m_scale,
		clip.height * m_scale,
	};
}

std::optional<ViewportRenderPlan> Viewport::renderPlan() const
{
	if (!positiveFinite(m_page.widthPoints) || !positiveFinite(m_page.heightPoints)
	    || !positiveFinite(m_viewportWidth) || !positiveFinite(m_viewportHeight) || !positiveFinite(m_scale))
		return std::nullopt;

	const double visibleWidth = std::min(m_page.widthPoints, m_viewportWidth / m_scale);
	const double visibleHeight = std::min(m_page.heightPoints, m_viewportHeight / m_scale);
	const double clipX = std::clamp(m_center.x - visibleWidth / 2.0, 0.0, m_page.widthPoints - visibleWidth);
	const double clipY = std::clamp(m_center.y - visibleHeight / 2.0, 0.0, m_page.heightPoints - visibleHeight);
	const double logicalWidth = visibleWidth * m_scale;
	const double logicalHeight = visibleHeight * m_scale;

	double outputWidth = std::max(1.0, std::round(logicalWidth * m_displayScale));
	double outputHeight = std::max(1.0, std::round(logicalHeight * m_displayScale));
	const double outputPixels = outputWidth * outputHeight;
	if (outputPixels > static_cast<double>(m_maxOutputPixels)) {
		const double reduction = std::sqrt(static_cast<double>(m_maxOutputPixels) / outputPixels);
		outputWidth = std::max(1.0, std::floor(outputWidth * reduction));
		outputHeight = std::max(1.0, std::floor(outputHeight * reduction));
	}

	const auto placement = placementFor({ clipX, clipY, visibleWidth, visibleHeight });
	if (!placement)
		return std::nullopt;

	return ViewportRenderPlan {
		{ m_pageIndex,
		        { clipX, clipY, visibleWidth, visibleHeight },
		        static_cast<std::uint32_t>(std::min(outputWidth, static_cast<double>(std::numeric_limits<std::uint32_t>::max()))),
		        static_cast<std::uint32_t>(std::min(outputHeight, static_cast<double>(std::numeric_limits<std::uint32_t>::max()))) },
		*placement,
	};
}

void Viewport::updateFitScale()
{
	if (!positiveFinite(m_page.widthPoints) || !positiveFinite(m_page.heightPoints)
	    || !positiveFinite(m_viewportWidth) || !positiveFinite(m_viewportHeight))
		return;

	if (m_fitMode == FitMode::page)
		m_scale = std::min(m_viewportWidth / m_page.widthPoints, m_viewportHeight / m_page.heightPoints);
	else if (m_fitMode == FitMode::width)
		m_scale = m_viewportWidth / m_page.widthPoints;
	clampCenter();
}

void Viewport::clampCenter()
{
	if (!positiveFinite(m_page.widthPoints) || !positiveFinite(m_page.heightPoints) || !positiveFinite(m_scale))
		return;

	const double halfVisibleWidth = std::min(m_page.widthPoints, m_viewportWidth / m_scale) / 2.0;
	const double halfVisibleHeight = std::min(m_page.heightPoints, m_viewportHeight / m_scale) / 2.0;
	m_center.x = std::clamp(m_center.x, halfVisibleWidth, m_page.widthPoints - halfVisibleWidth);
	m_center.y = std::clamp(m_center.y, halfVisibleHeight, m_page.heightPoints - halfVisibleHeight);
}

} // namespace mecaps::pdf