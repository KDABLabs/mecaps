#pragma once

#include "pdf.h"

#include <cstddef>
#include <optional>

namespace mecaps::pdf {

enum class FitMode {
	page,
	width,
	custom,
};

struct ViewportPoint {
	double x { 0.0 };
	double y { 0.0 };
};

struct ViewportRect {
	double x { 0.0 };
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
};

struct ViewportRenderPlan {
	RenderRequest request;
	ViewportRect placement;
};

// Allowed panBy delta ranges, measured in logical viewport pixels.
struct PanBounds {
	double minimumX { 0.0 };
	double maximumX { 0.0 };
	double minimumY { 0.0 };
	double maximumY { 0.0 };
};

class Viewport
{
  public:
	explicit Viewport(std::size_t maxOutputPixels = defaultMaxOutputPixels);

	void setPage(std::size_t pageIndex, PageMetadata page);
	void setViewport(double width, double height, double displayScale = 1.0);
	void setFitMode(FitMode mode);
	void zoomBy(double factor, ViewportPoint anchor);
	[[nodiscard]] bool panBy(double deltaX, double deltaY);

	[[nodiscard]] FitMode fitMode() const noexcept { return m_fitMode; }
	[[nodiscard]] double scale() const noexcept { return m_scale; }
	[[nodiscard]] ViewportPoint center() const noexcept { return m_center; }
	[[nodiscard]] PanBounds panBounds() const noexcept;
	[[nodiscard]] std::optional<ViewportRect> placementFor(PageClip clip) const;
	[[nodiscard]] std::optional<ViewportRenderPlan> renderPlan() const;

  private:
	void updateFitScale();
	void clampCenter();

	std::size_t m_maxOutputPixels;
	std::size_t m_pageIndex { 0 };
	PageMetadata m_page;
	double m_viewportWidth { 0.0 };
	double m_viewportHeight { 0.0 };
	double m_displayScale { 1.0 };
	double m_scale { 1.0 };
	ViewportPoint m_center;
	FitMode m_fitMode { FitMode::page };
};

} // namespace mecaps::pdf