#pragma once

#include "pdf.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace mecaps::pdf {

using GenerationId = std::uint64_t;
using Completion = std::function<void()>;
using Dispatcher = std::function<void(Completion)>;

struct ControllerLimits {
	std::size_t maxDocumentBytes { 64U * 1024U * 1024U };
	std::size_t maxPageCount { 10'000U };
	double maxPageWidthPoints { 20'000.0 };
	double maxPageHeightPoints { 20'000.0 };
	std::size_t maxOutputPixels { 16U * 1024U * 1024U };
	std::size_t maxInFlightRequests { 16U };
};

struct OpenResult {
	GenerationId generation { 0 };
	std::size_t pageCount { 0 };
	DocumentError error { DocumentError::none };
};

struct PageMetadataResult {
	GenerationId generation { 0 };
	std::size_t pageIndex { 0 };
	PageMetadata metadata;
	DocumentError error { DocumentError::none };
};

struct RenderResult {
	GenerationId generation { 0 };
	RenderRequest request;
	std::vector<std::byte> pixels;
	std::uint32_t strideBytes { 0 };
	PixelFormat format { PixelFormat::rgba8 };
	DocumentError error { DocumentError::none };
};

class Controller
{
  public:
	using OpenCallback = std::function<void(OpenResult)>;
	using PageMetadataCallback = std::function<void(PageMetadataResult)>;
	using RenderCallback = std::function<void(RenderResult)>;

	Controller(std::unique_ptr<Backend> backend, Dispatcher dispatcher, ControllerLimits limits = {});
	~Controller();

	Controller(const Controller &) = delete;
	Controller &operator=(const Controller &) = delete;
	Controller(Controller &&) = delete;
	Controller &operator=(Controller &&) = delete;

	void open(GenerationId generation, std::vector<std::byte> documentData, OpenCallback callback);
	void pageMetadata(GenerationId generation, std::size_t pageIndex, PageMetadataCallback callback);
	void render(
	        GenerationId generation,
	        RenderRequest request,
	        PixelFormat format,
	        RenderCallback callback);

  private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace mecaps::pdf