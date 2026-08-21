#include "pdfium_backend.h"

#include <fpdf_progressive.h>
#include <fpdfview.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace mecaps::pdf {
namespace {

class PdfiumRuntime
{
  public:
	PdfiumRuntime() { FPDF_InitLibrary(); }
	~PdfiumRuntime() { FPDF_DestroyLibrary(); }

	std::mutex mutex;
};

std::shared_ptr<PdfiumRuntime> runtime()
{
	static auto instance = std::make_shared<PdfiumRuntime>();
	return instance;
}

DocumentError mapLastError() noexcept
{
	switch (FPDF_GetLastError()) {
	case FPDF_ERR_FILE:
	case FPDF_ERR_FORMAT:
		return DocumentError::invalidDocument;
	case FPDF_ERR_PASSWORD:
		return DocumentError::passwordRequired;
	case FPDF_ERR_SECURITY:
		return DocumentError::unsupportedDocument;
	default:
		return DocumentError::backendFailure;
	}
}

class PageHandle
{
  public:
	explicit PageHandle(FPDF_PAGE page)
	    : m_page(page)
	{
	}

	~PageHandle()
	{
		if (m_page)
			FPDF_ClosePage(m_page);
	}

	[[nodiscard]] FPDF_PAGE get() const noexcept { return m_page; }

  private:
	FPDF_PAGE m_page;
};

class DocumentHandle
{
  public:
	explicit DocumentHandle(FPDF_DOCUMENT document)
	    : m_document(document)
	{
	}

	~DocumentHandle()
	{
		if (m_document)
			FPDF_CloseDocument(m_document);
	}

	[[nodiscard]] FPDF_DOCUMENT get() const noexcept { return m_document; }
	FPDF_DOCUMENT release() noexcept { return std::exchange(m_document, nullptr); }

  private:
	FPDF_DOCUMENT m_document;
};

class BitmapHandle
{
  public:
	explicit BitmapHandle(FPDF_BITMAP bitmap)
	    : m_bitmap(bitmap)
	{
	}

	~BitmapHandle()
	{
		if (m_bitmap)
			FPDFBitmap_Destroy(m_bitmap);
	}

	[[nodiscard]] FPDF_BITMAP get() const noexcept { return m_bitmap; }

  private:
	FPDF_BITMAP m_bitmap;
};

class ProgressiveRender
{
  public:
	explicit ProgressiveRender(FPDF_PAGE page)
	    : m_page(page)
	{
	}

	~ProgressiveRender()
	{
		if (m_started)
			FPDF_RenderPage_Close(m_page);
	}

	void started() noexcept { m_started = true; }

  private:
	FPDF_PAGE m_page;
	bool m_started { false };
};

class PdfiumDocument final : public Document
{
  public:
	PdfiumDocument(std::shared_ptr<PdfiumRuntime> runtime, std::vector<std::byte> data,
	               FPDF_DOCUMENT document) noexcept
	    : m_runtime(std::move(runtime))
	    , m_data(std::move(data))
	    , m_document(document)
	    , m_pageCount(static_cast<std::size_t>(FPDF_GetPageCount(document)))
	{
	}

	~PdfiumDocument() override
	{
		std::scoped_lock lock(m_runtime->mutex);
		FPDF_CloseDocument(m_document);
	}

	[[nodiscard]] std::size_t pageCount() const noexcept override { return m_pageCount; }

	[[nodiscard]] Result<PageMetadata> pageMetadata(std::size_t pageIndex) const noexcept override
	{
		if (pageIndex >= m_pageCount)
			return { {}, DocumentError::pageOutOfBounds };

		std::scoped_lock lock(m_runtime->mutex);
		PageHandle page(FPDF_LoadPage(m_document, static_cast<int>(pageIndex)));
		if (!page.get())
			return { {}, DocumentError::backendFailure };

		return { { FPDF_GetPageWidth(page.get()), FPDF_GetPageHeight(page.get()) } };
	}

	void beginRender() noexcept override { m_cancelled.store(false, std::memory_order_relaxed); }

	[[nodiscard]] Result<void> render(const RenderRequest &request, PixelBuffer buffer) noexcept override
	{
		if (request.pageIndex >= m_pageCount)
			return { DocumentError::pageOutOfBounds };

		std::scoped_lock lock(m_runtime->mutex);
		PageHandle page(FPDF_LoadPage(m_document, static_cast<int>(request.pageIndex)));
		if (!page.get())
			return { DocumentError::backendFailure };

		const PageMetadata metadata { FPDF_GetPageWidth(page.get()), FPDF_GetPageHeight(page.get()) };
		if (const auto error = validateRenderRequest(metadata, request, buffer); error != DocumentError::none)
			return { error };

		if (request.outputWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
		    || request.outputHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
		    || buffer.strideBytes > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
			return { DocumentError::resourceLimit };
		}

		const auto scaleX = static_cast<double>(request.outputWidth) / request.clip.width;
		const auto scaleY = static_cast<double>(request.outputHeight) / request.clip.height;
		const auto pageWidth = metadata.widthPoints * scaleX;
		const auto pageHeight = metadata.heightPoints * scaleY;
		const auto offsetX = -request.clip.x * scaleX;
		const auto offsetY = -request.clip.y * scaleY;
		if (!fitsInt(pageWidth) || !fitsInt(pageHeight) || !fitsInt(offsetX) || !fitsInt(offsetY))
			return { DocumentError::resourceLimit };

		const auto bitmapFormat = buffer.format == PixelFormat::rgb8 ? FPDFBitmap_BGR : FPDFBitmap_BGRA;
		BitmapHandle bitmap(FPDFBitmap_CreateEx(
		        static_cast<int>(buffer.width),
		        static_cast<int>(buffer.height),
		        bitmapFormat,
		        buffer.pixels.data(),
		        static_cast<int>(buffer.strideBytes)));
		if (!bitmap.get())
			return { DocumentError::resourceLimit };

		if (!FPDFBitmap_FillRect(bitmap.get(), 0, 0, static_cast<int>(buffer.width),
		                           static_cast<int>(buffer.height), 0xffffffff)) {
			return { DocumentError::backendFailure };
		}

		IFSDK_PAUSE pause { 1, &needToPause, &m_cancelled };
		ProgressiveRender progressive(page.get());
		int status = FPDF_RenderPageBitmap_Start(
		        bitmap.get(), page.get(), static_cast<int>(std::lround(offsetX)),
		        static_cast<int>(std::lround(offsetY)), static_cast<int>(std::lround(pageWidth)),
		        static_cast<int>(std::lround(pageHeight)), 0, FPDF_ANNOT, &pause);
		progressive.started();

		while (status == FPDF_RENDER_TOBECONTINUED && !m_cancelled.load(std::memory_order_relaxed))
			status = FPDF_RenderPage_Continue(page.get(), &pause);

		if (status == FPDF_RENDER_TOBECONTINUED)
			return { DocumentError::cancelled };
		if (status != FPDF_RENDER_DONE)
			return { DocumentError::backendFailure };

		convertToContractFormat(buffer);
		return {};
	}

	void cancel() noexcept override { m_cancelled.store(true, std::memory_order_relaxed); }

  private:
	static bool fitsInt(double value) noexcept
	{
		return std::isfinite(value) && value >= static_cast<double>(std::numeric_limits<int>::min())
		       && value <= static_cast<double>(std::numeric_limits<int>::max());
	}

	static FPDF_BOOL needToPause(IFSDK_PAUSE *pause) noexcept
	{
		return static_cast<std::atomic_bool *>(pause->user)->load(std::memory_order_relaxed);
	}

	static void convertToContractFormat(PixelBuffer buffer) noexcept
	{
		const auto pixelSize = bytesPerPixel(buffer.format);
		for (std::uint32_t y = 0; y < buffer.height; ++y) {
			auto *row = buffer.pixels.data() + static_cast<std::size_t>(y) * buffer.strideBytes;
			for (std::uint32_t x = 0; x < buffer.width; ++x)
				std::swap(row[x * pixelSize], row[x * pixelSize + 2]);
		}
	}

	std::shared_ptr<PdfiumRuntime> m_runtime;
	std::vector<std::byte> m_data;
	FPDF_DOCUMENT m_document;
	std::size_t m_pageCount;
	std::atomic_bool m_cancelled { false };
};

class PdfiumBackend final : public Backend
{
  public:
	PdfiumBackend()
	    : m_runtime(runtime())
	{
	}

	[[nodiscard]] Result<std::unique_ptr<Document>> open(std::vector<std::byte> documentData) noexcept override
	{
		if (documentData.empty())
			return { nullptr, DocumentError::invalidDocument };

		try {
			std::scoped_lock lock(m_runtime->mutex);
			DocumentHandle document(FPDF_LoadMemDocument64(documentData.data(), documentData.size(), nullptr));
			if (!document.get())
				return { nullptr, mapLastError() };
			auto result =
			        std::make_unique<PdfiumDocument>(m_runtime, std::move(documentData), document.get());
			// Moving the vector preserves the heap pointer retained by PDFium.
			document.release();
			return { std::move(result) };
		} catch (const std::bad_alloc &) {
			return { nullptr, DocumentError::resourceLimit };
		} catch (...) {
			return { nullptr, DocumentError::backendFailure };
		}
	}

  private:
	std::shared_ptr<PdfiumRuntime> m_runtime;
};

} // namespace

std::unique_ptr<Backend> createPdfiumBackend()
{
	return std::make_unique<PdfiumBackend>();
}

} // namespace mecaps::pdf
