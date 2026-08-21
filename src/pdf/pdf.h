#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mecaps::pdf {

enum class DocumentError {
	none,
	invalidDocument,
	passwordRequired,
	unsupportedDocument,
	pageOutOfBounds,
	invalidRenderRequest,
	resourceLimit,
	cancelled,
	backendFailure,
};

struct PageMetadata {
	double widthPoints { 0.0 };
	double heightPoints { 0.0 };
};

struct PageClip {
	double x { 0.0 };
	double y { 0.0 };
	double width { 0.0 };
	double height { 0.0 };
};

struct RenderRequest {
	std::size_t pageIndex { 0 };
	PageClip clip;
	std::uint32_t outputWidth { 0 };
	std::uint32_t outputHeight { 0 };
};

enum class PixelFormat : std::uint8_t {
	rgb8 = 3,
	rgba8 = 4,
};

[[nodiscard]] constexpr std::size_t bytesPerPixel(PixelFormat format) noexcept
{
	switch (format) {
	case PixelFormat::rgb8:
		return 3;
	case PixelFormat::rgba8:
		return 4;
	}
	return 0;
}

struct PixelBuffer {
	std::span<std::byte> pixels;
	std::uint32_t width { 0 };
	std::uint32_t height { 0 };
	std::uint32_t strideBytes { 0 };
	PixelFormat format { PixelFormat::rgba8 };
};

template<typename T>
struct Result {
	T value;
	DocumentError error { DocumentError::none };

	[[nodiscard]] explicit operator bool() const noexcept { return error == DocumentError::none; }
};

template<>
struct Result<void> {
	DocumentError error { DocumentError::none };

	[[nodiscard]] explicit operator bool() const noexcept { return error == DocumentError::none; }
};

class Document
{
  public:
	virtual ~Document() = default;

	[[nodiscard]] virtual std::size_t pageCount() const noexcept = 0;
	[[nodiscard]] virtual Result<PageMetadata> pageMetadata(std::size_t pageIndex) const noexcept = 0;
	[[nodiscard]] virtual Result<void> render(const RenderRequest &request, PixelBuffer buffer) noexcept = 0;
	virtual void cancel() noexcept = 0;
};

class Backend
{
  public:
	virtual ~Backend() = default;

	[[nodiscard]] virtual Result<std::unique_ptr<Document>> open(std::vector<std::byte> documentData) noexcept = 0;
};

[[nodiscard]] DocumentError validateRenderRequest(
        const PageMetadata &page,
        const RenderRequest &request,
        const PixelBuffer &buffer) noexcept;

} // namespace mecaps::pdf