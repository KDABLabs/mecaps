#include "pdf.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <array>
#include <utility>

namespace {

using namespace mecaps::pdf;

static_assert(bytesPerPixel(PixelFormat::rgb8) == 3);
static_assert(bytesPerPixel(PixelFormat::rgba8) == 4);

class FakeDocument final : public Document
{
  public:
	explicit FakeDocument(bool honorCancellation = true)
	    : m_honorCancellation(honorCancellation)
	{
	}

	[[nodiscard]] std::size_t pageCount() const noexcept override { return 1; }

	[[nodiscard]] Result<PageMetadata> pageMetadata(std::size_t pageIndex) const noexcept override
	{
		if (pageIndex >= pageCount())
			return { {}, DocumentError::pageOutOfBounds };
		return { { 612.0, 792.0 } };
	}

	void beginRender() noexcept override { m_cancelled = false; }

	[[nodiscard]] Result<void> render(const RenderRequest &request, PixelBuffer buffer) noexcept override
	{
		if (m_cancelled && m_honorCancellation)
			return { DocumentError::cancelled };

		const auto page = pageMetadata(request.pageIndex);
		if (!page)
			return { page.error };
		return { validateRenderRequest(page.value, request, buffer) };
	}

	void cancel() noexcept override { m_cancelled = true; }

  private:
	bool m_honorCancellation;
	bool m_cancelled { false };
};

class FakeBackend final : public Backend
{
  public:
	[[nodiscard]] Result<std::unique_ptr<Document>> open(std::vector<std::byte> data) noexcept override
	{
		if (data.empty())
			return { nullptr, DocumentError::invalidDocument };
		return { std::make_unique<FakeDocument>() };
	}
};

TEST_SUITE("PDF contract")
{
	TEST_CASE("document errors are returned without a backend exception")
	{
		FakeBackend backend;
		const auto result = backend.open({});

		CHECK_FALSE(result);
		CHECK(result.error == DocumentError::invalidDocument);
	}

	TEST_CASE("page queries reject an out-of-bounds index")
	{
		FakeDocument document;

		CHECK(document.pageMetadata(0));
		CHECK(document.pageMetadata(1).error == DocumentError::pageOutOfBounds);
	}

	TEST_CASE("render requests require an in-page clip and matching caller-owned buffer")
	{
		FakeDocument document;
		std::array<std::byte, 4 * 4 * 4> storage {};
		PixelBuffer buffer { storage, 4, 4, 16, PixelFormat::rgba8 };
		RenderRequest request { 0, { 0.0, 0.0, 612.0, 792.0 }, 4, 4 };

		CHECK(document.render(request, buffer));

		request.clip.x = 1.0;
		CHECK(document.render(request, buffer).error == DocumentError::invalidRenderRequest);
		request.clip = { 0.0, 0.0, 612.0, 792.0 };
		buffer.strideBytes = 15;
		CHECK(document.render(request, buffer).error == DocumentError::invalidRenderRequest);
		buffer.strideBytes = 16;
		buffer.format = static_cast<PixelFormat>(0xff);
		CHECK(document.render(request, buffer).error == DocumentError::invalidRenderRequest);
	}

	TEST_CASE("render requests accept a floating-point clip aligned to the page edge")
	{
		constexpr PageMetadata page { 1022.5983152650803, 792.0 };
		constexpr double clipWidth = 414.14447333636707;
		constexpr double clipX = page.widthPoints - clipWidth;
		std::array<std::byte, 4> storage {};
		const PixelBuffer buffer { storage, 1, 1, 4, PixelFormat::rgba8 };
		const RenderRequest request { 0, { clipX, 0.0, clipWidth, page.heightPoints }, 1, 1 };

		REQUIRE(clipX + clipWidth > page.widthPoints);
		CHECK(validateRenderRequest(page, request, buffer) == DocumentError::none);
	}

	TEST_CASE("cancellation is best effort")
	{
		std::array<std::byte, 4> storage {};
		const RenderRequest request { 0, { 0.0, 0.0, 612.0, 792.0 }, 1, 1 };
		const PixelBuffer buffer { storage, 1, 1, 4, PixelFormat::rgba8 };

		FakeDocument cancellable;
		cancellable.beginRender();
		cancellable.cancel();
		CHECK(cancellable.render(request, buffer).error == DocumentError::cancelled);

		FakeDocument completing(false);
		completing.beginRender();
		completing.cancel();
		CHECK(completing.render(request, buffer));
	}
}

} // namespace