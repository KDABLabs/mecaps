#include "pdf_viewport.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace {

using namespace mecaps::pdf;

TEST_SUITE("PDF viewport")
{
	TEST_CASE("fit page and fit width track viewport and display scale")
	{
		Viewport viewport;
		viewport.setPage(3, { 600.0, 800.0 });
		viewport.setViewport(300.0, 300.0, 2.0);

		auto plan = viewport.renderPlan();
		REQUIRE(plan);
		CHECK(viewport.scale() == doctest::Approx(0.375));
		CHECK(plan->request.outputWidth == 450);
		CHECK(plan->request.outputHeight == 600);
		CHECK(plan->placement.x == doctest::Approx(37.5));

		viewport.setFitMode(FitMode::width);
		plan = viewport.renderPlan();
		REQUIRE(plan);
		CHECK(viewport.scale() == doctest::Approx(0.5));
		CHECK(plan->request.clip.height == doctest::Approx(600.0));
		CHECK(plan->request.outputWidth == 600);
		CHECK(plan->request.outputHeight == 600);

		viewport.setViewport(450.0, 300.0, 2.0);
		CHECK(viewport.scale() == doctest::Approx(0.75));
	}

	TEST_CASE("zoom preserves the pointer document anchor")
	{
		Viewport viewport;
		viewport.setPage(0, { 1000.0, 1000.0 });
		viewport.setViewport(500.0, 500.0);
		const ViewportPoint anchor { 400.0, 300.0 };
		const auto before = viewport.center();
		const ViewportPoint documentBefore {
			before.x + (anchor.x - 250.0) / viewport.scale(),
			before.y + (anchor.y - 250.0) / viewport.scale(),
		};

		viewport.zoomBy(2.0, anchor);
		const auto after = viewport.center();
		CHECK(after.x + (anchor.x - 250.0) / viewport.scale() == doctest::Approx(documentBefore.x));
		CHECK(after.y + (anchor.y - 250.0) / viewport.scale() == doctest::Approx(documentBefore.y));
		CHECK(viewport.fitMode() == FitMode::custom);
	}

	TEST_CASE("clipping and panning remain inside page bounds")
	{
		Viewport viewport;
		viewport.setPage(0, { 1000.0, 800.0 });
		viewport.setViewport(400.0, 300.0);
		viewport.zoomBy(4.0, { 200.0, 150.0 });
		REQUIRE(viewport.panBy(-100000.0, -100000.0));

		const auto plan = viewport.renderPlan();
		REQUIRE(plan);
		CHECK(plan->request.clip.x >= 0.0);
		CHECK(plan->request.clip.y >= 0.0);
		CHECK(plan->request.clip.x + plan->request.clip.width == doctest::Approx(1000.0));
		CHECK(plan->request.clip.y + plan->request.clip.height == doctest::Approx(800.0));
		CHECK(plan->placement.x == doctest::Approx(0.0));
		CHECK(plan->placement.y == doctest::Approx(0.0));
	}

	TEST_CASE("pan bounds stop motion and rendering requests at page edges")
	{
		Viewport viewport;
		viewport.setPage(0, { 1000.0, 800.0 });
		viewport.setViewport(400.0, 300.0);
		viewport.zoomBy(4.0, { 200.0, 150.0 });

		auto bounds = viewport.panBounds();
		CHECK(bounds.minimumX < 0.0);
		CHECK(bounds.maximumX > 0.0);
		REQUIRE(viewport.panBy(bounds.maximumX, 0.0));
		bounds = viewport.panBounds();
		CHECK(bounds.maximumX == doctest::Approx(0.0));
		CHECK_FALSE(viewport.panBy(1.0, 0.0));
	}

	TEST_CASE("render output respects the controller pixel limit")
	{
		Viewport viewport(10'000);
		viewport.setPage(0, { 1000.0, 1000.0 });
		viewport.setViewport(500.0, 500.0, 4.0);
		const auto plan = viewport.renderPlan();
		REQUIRE(plan);
		CHECK(static_cast<std::size_t>(plan->request.outputWidth) * plan->request.outputHeight <= 10'000);
		CHECK(plan->placement.width == doctest::Approx(500.0));
		CHECK(plan->placement.height == doctest::Approx(500.0));
	}

	TEST_CASE("an existing raster follows preview zoom and pan")
	{
		Viewport viewport;
		viewport.setPage(0, { 1000.0, 1000.0 });
		viewport.setViewport(500.0, 500.0);
		const PageClip renderedPage { 0.0, 0.0, 1000.0, 1000.0 };

		viewport.zoomBy(2.0, { 250.0, 250.0 });
		REQUIRE(viewport.panBy(50.0, -25.0));
		const auto placement = viewport.placementFor(renderedPage);
		REQUIRE(placement);
		CHECK(placement->x == doctest::Approx(-200.0));
		CHECK(placement->y == doctest::Approx(-275.0));
		CHECK(placement->width == doctest::Approx(1000.0));
		CHECK(placement->height == doctest::Approx(1000.0));
	}
}

} // namespace