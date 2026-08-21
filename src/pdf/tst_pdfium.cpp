#include "pdfium_backend.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace mecaps::pdf;

std::string loadFixture(std::string_view name = "two_colors.pdf")
{
	std::ifstream input(std::string(PDF_TEST_FIXTURE_DIR) + "/" + std::string(name), std::ios::binary);
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

Result<std::unique_ptr<Document>> openFixture(Backend &backend)
{
	const auto fixture = loadFixture();
	const auto bytes = std::as_bytes(std::span(fixture));
	return backend.open({ bytes.begin(), bytes.end() });
}

std::uint8_t component(std::byte value)
{
	return std::to_integer<std::uint8_t>(value);
}

TEST_SUITE("PDFium backend")
{
	TEST_CASE("maps malformed input to the contract error")
	{
		auto backend = createPdfiumBackend();
		const std::array malformed { std::byte { 'n' }, std::byte { 'o' }, std::byte { 't' } };

		const auto result = backend->open({ malformed.begin(), malformed.end() });

		CHECK_FALSE(result);
		CHECK(result.error == DocumentError::invalidDocument);
	}

	TEST_CASE("reports page count, dimensions, and bounds")
	{
		auto backend = createPdfiumBackend();
		auto result = openFixture(*backend);
		REQUIRE(result);

		CHECK(result.value->pageCount() == 1);
		const auto metadata = result.value->pageMetadata(0);
		REQUIRE(metadata);
		CHECK(metadata.value.widthPoints == doctest::Approx(20.0));
		CHECK(metadata.value.heightPoints == doctest::Approx(10.0));
		CHECK(result.value->pageMetadata(1).error == DocumentError::pageOutOfBounds);
	}

	TEST_CASE("opens and renders the two-page A4 demo document")
	{
		auto backend = createPdfiumBackend();
		const auto fixture = loadFixture("demo_letter.pdf");
		REQUIRE(fixture.starts_with("%PDF-1.4"));
		auto result = backend->open(std::as_bytes(std::span(fixture)));
		REQUIRE(result);
		REQUIRE(result.value->pageCount() == 2);

		for (std::size_t pageIndex = 0; pageIndex < result.value->pageCount(); ++pageIndex) {
			const auto metadata = result.value->pageMetadata(pageIndex);
			REQUIRE(metadata);
			CHECK(metadata.value.widthPoints == doctest::Approx(595.0));
			CHECK(metadata.value.heightPoints == doctest::Approx(842.0));

			std::array<std::byte, 60 * 85 * 3> pixels {};
			const RenderRequest request { pageIndex, { 0.0, 0.0, 595.0, 842.0 }, 60, 85 };
			const PixelBuffer buffer { pixels, 60, 85, 180, PixelFormat::rgb8 };
			CHECK(result.value->render(request, buffer));
		}
	}

	TEST_CASE("renders and converts PDFium BGR output to RGB")
	{
		auto backend = createPdfiumBackend();
		auto result = openFixture(*backend);
		REQUIRE(result);

		std::array<std::byte, 20 * 10 * 3> pixels {};
		const RenderRequest request { 0, { 0.0, 0.0, 20.0, 10.0 }, 20, 10 };
		const PixelBuffer buffer { pixels, 20, 10, 60, PixelFormat::rgb8 };
		REQUIRE(result.value->render(request, buffer));

		const auto left = 5 * 3;
		const auto right = 15 * 3;
		CHECK(component(pixels[left]) > 240);
		CHECK(component(pixels[left + 1]) < 15);
		CHECK(component(pixels[left + 2]) < 15);
		CHECK(component(pixels[right]) < 15);
		CHECK(component(pixels[right + 1]) > 240);
		CHECK(component(pixels[right + 2]) < 15);
	}

	TEST_CASE("renders a clip to RGBA without touching padding or guards")
	{
		auto backend = createPdfiumBackend();
		auto result = openFixture(*backend);
		REQUIRE(result);

		constexpr auto guard = std::byte { 0xa5 };
		std::array<std::byte, 32> storage;
		storage.fill(guard);
		const RenderRequest request { 0, { 10.0, 0.0, 10.0, 10.0 }, 2, 2 };
		const PixelBuffer buffer { std::span(storage).subspan(4, 24), 2, 2, 12, PixelFormat::rgba8 };
		REQUIRE(result.value->render(request, buffer));

		CHECK(std::all_of(storage.begin(), storage.begin() + 4, [guard](auto value) { return value == guard; }));
		CHECK(std::all_of(storage.begin() + 12, storage.begin() + 16,
		                  [guard](auto value) { return value == guard; }));
		CHECK(std::all_of(storage.begin() + 24, storage.begin() + 28,
		                  [guard](auto value) { return value == guard; }));
		CHECK(std::all_of(storage.end() - 4, storage.end(), [guard](auto value) { return value == guard; }));

		CHECK(component(storage[4]) < 15);
		CHECK(component(storage[5]) > 240);
		CHECK(component(storage[6]) < 15);
		CHECK(component(storage[7]) == 255);
	}

	TEST_CASE("cancellation only affects an in-flight render")
	{
		auto backend = createPdfiumBackend();
		auto result = openFixture(*backend);
		REQUIRE(result);

		std::array<std::byte, 20 * 10 * 4> pixels {};
		const RenderRequest request { 0, { 0.0, 0.0, 20.0, 10.0 }, 20, 10 };
		const PixelBuffer buffer { pixels, 20, 10, 80, PixelFormat::rgba8 };
		result.value->cancel();

		CHECK(result.value->render(request, buffer));
		CHECK(result.value->render(request, buffer));
	}
}

} // namespace
