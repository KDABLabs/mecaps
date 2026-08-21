#include "pdf_demo.h"

#include <KDUtils/dir.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t maxDocumentBytes = 64U * 1024U * 1024U;
constexpr std::uint32_t maxRenderWidth = 1200;
constexpr std::uint32_t maxRenderHeight = 1600;

void reportFileDispatchFailure(mecaps::pdf::DocumentError error) noexcept
{
	try {
		spdlog::error("Failed to dispatch PDF file completion (error {})", static_cast<int>(error));
	} catch (...) {
	}
}

std::string_view errorMessage(mecaps::pdf::DocumentError error)
{
	using mecaps::pdf::DocumentError;
	switch (error) {
	case DocumentError::invalidDocument: return "The document is not a valid PDF.";
	case DocumentError::passwordRequired: return "Password-protected PDFs are not supported.";
	case DocumentError::unsupportedDocument: return "This PDF uses unsupported features.";
	case DocumentError::pageOutOfBounds: return "The requested page does not exist.";
	case DocumentError::invalidRenderRequest: return "The page could not be rendered at this size.";
	case DocumentError::resourceLimit: return "The PDF exceeds the configured resource limits.";
	case DocumentError::cancelled: return "Rendering was cancelled.";
	case DocumentError::backendFailure: return "The PDF renderer failed.";
	case DocumentError::none: return "";
	}
	return "The PDF operation failed.";
}

} // namespace

PdfDemo::PdfDemo(const PdfSingleton &ui, std::unique_ptr<mecaps::pdf::Backend> backend)
    : m_ui(ui)
    , m_controller(std::make_unique<mecaps::pdf::Controller>(
              std::move(backend),
              [](mecaps::pdf::Completion completion) {
		      slint::invoke_from_event_loop(std::move(completion));
	      }))
	, m_fileWorker([this] { runFileWorker(); })
{
	const auto documentPath = KDUtils::Dir::applicationDir().absoluteFilePath("two_colors.pdf");
	m_ui.set_document_path(slint::SharedString(documentPath));
	m_ui.on_request_open([this](const slint::SharedString &path) { open(path); });
	m_ui.on_request_previous([this] {
		if (m_pageIndex > 0)
			showPage(m_pageIndex - 1);
	});
	m_ui.on_request_next([this] {
		if (m_pageIndex + 1 < m_pageCount)
			showPage(m_pageIndex + 1);
	});
}

PdfDemo::~PdfDemo()
{
	m_ui.on_request_open([](const slint::SharedString &) {});
	m_ui.on_request_previous([] {});
	m_ui.on_request_next([] {});
	m_ui.on_viewport_changed([](float, float, float) {});
	m_ui.on_request_fit_page([] {});
	m_ui.on_request_fit_width([] {});
	m_ui.on_request_zoom_in([](float, float) {});
	m_ui.on_request_zoom_out([](float, float) {});
	m_ui.on_request_pan([](float, float, bool) {});
	m_publication->alive.store(false, std::memory_order_release);
	{
		const std::scoped_lock lock(m_fileMutex);
		m_stopping = true;
		m_fileRequest.reset();
	}
	m_fileReady.notify_one();
	if (m_fileWorker.joinable())
		m_fileWorker.join();
}

void PdfDemo::open(const slint::SharedString &path)
{
	const auto generation = ++m_generation;
	m_publication->generation.store(generation, std::memory_order_release);
	m_ui.set_loading(true);
	m_ui.set_error_message("");
	m_ui.set_page_image({});
	m_ui.set_page_number(0);
	m_ui.set_page_count(0);
	m_pageCount = 0;
	m_controller->open(generation, {}, [](mecaps::pdf::OpenResult) {});

	{
		const std::scoped_lock lock(m_fileMutex);
		m_fileRequest = FileRequest { generation, std::string(std::string_view(path)) };
	}
	m_fileReady.notify_one();
}

void PdfDemo::runFileWorker()
{
	for (;;) {
		FileRequest request;
		{
			std::unique_lock lock(m_fileMutex);
			m_fileReady.wait(lock, [this] { return m_stopping || m_fileRequest.has_value(); });
			if (m_stopping)
				return;
			request = std::move(*m_fileRequest);
			m_fileRequest.reset();
		}

		std::vector<std::byte> data;
		std::string_view error;
		try {
			std::ifstream stream(request.path, std::ios::binary | std::ios::ate);
			if (!stream) {
				error = "Could not open the PDF file.";
			} else {
				const auto end = stream.tellg();
				if (end < 0 || static_cast<std::uintmax_t>(end) > maxDocumentBytes) {
					error = errorMessage(mecaps::pdf::DocumentError::resourceLimit);
				} else {
					data.resize(static_cast<std::size_t>(end));
					stream.seekg(0);
					if (!data.empty() && !stream.read(
					                             reinterpret_cast<char *>(data.data()),
					                             static_cast<std::streamsize>(data.size()))) {
						data.clear();
						error = "Could not read the PDF file.";
					}
				}
			}
		} catch (const std::bad_alloc &) {
			data.clear();
			error = errorMessage(mecaps::pdf::DocumentError::resourceLimit);
		} catch (...) {
			data.clear();
			error = errorMessage(mecaps::pdf::DocumentError::backendFailure);
		}

		const std::weak_ptr<PublicationState> weakPublication = m_publication;
		try {
			slint::invoke_from_event_loop(
			        [weakPublication, this, generation = request.generation,
			                data = std::move(data), error]() mutable {
				        const auto publication = weakPublication.lock();
				        if (publication && publication->alive.load(std::memory_order_acquire)
				            && publication->generation.load(std::memory_order_acquire) == generation) {
					        completeFileRead(generation, std::move(data), error);
				        }
			        });
		} catch (const std::bad_alloc &) {
			reportFileDispatchFailure(mecaps::pdf::DocumentError::resourceLimit);
		} catch (...) {
			reportFileDispatchFailure(mecaps::pdf::DocumentError::backendFailure);
		}
	}
}

void PdfDemo::completeFileRead(
        mecaps::pdf::GenerationId generation,
        std::vector<std::byte> data,
	std::string_view error)
{
	if (!error.empty()) {
		m_ui.set_loading(false);
		m_ui.set_error_message(slint::SharedString(error));
		return;
	}

	m_controller->open(generation, std::move(data), [this](mecaps::pdf::OpenResult result) {
		if (result.error != mecaps::pdf::DocumentError::none) {
			publishError(result.error);
			return;
		}
		m_pageCount = result.pageCount;
		m_ui.set_page_count(static_cast<int>(std::min(
		        m_pageCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
		showPage(0);
	});
}

void PdfDemo::showPage(std::size_t pageIndex)
{
	if (pageIndex >= m_pageCount)
		return;

	const auto generation = ++m_generation;
	m_ui.set_loading(true);
	m_ui.set_error_message("");
	m_controller->pageMetadata(generation, pageIndex, [this](mecaps::pdf::PageMetadataResult result) {
		if (result.error != mecaps::pdf::DocumentError::none) {
			publishError(result.error);
			return;
		}

		const auto scale = std::min(
		        static_cast<double>(maxRenderWidth) / result.metadata.widthPoints,
		        static_cast<double>(maxRenderHeight) / result.metadata.heightPoints);
		const auto width = static_cast<std::uint32_t>(std::max(1.0, std::round(result.metadata.widthPoints * scale)));
		const auto height = static_cast<std::uint32_t>(std::max(1.0, std::round(result.metadata.heightPoints * scale)));
		mecaps::pdf::RenderRequest request {
			result.pageIndex,
			{ 0, 0, result.metadata.widthPoints, result.metadata.heightPoints },
			width,
			height,
		};
		m_controller->render(result.generation, request, mecaps::pdf::PixelFormat::rgb8,
		        [this](mecaps::pdf::RenderResult render) {
			        if (render.error != mecaps::pdf::DocumentError::none) {
				        publishError(render.error);
				        return;
			        }

			        static_assert(sizeof(slint::Rgb8Pixel) == 3);
			        slint::SharedPixelBuffer<slint::Rgb8Pixel> pixels(
			                render.request.outputWidth, render.request.outputHeight);
			        const auto destinationBytes = static_cast<std::size_t>(pixels.end() - pixels.begin())
			                * sizeof(slint::Rgb8Pixel);
			        if (destinationBytes != render.pixels.size()) {
				        publishError(mecaps::pdf::DocumentError::backendFailure);
				        return;
			        }
			        std::memcpy(pixels.begin(), render.pixels.data(), destinationBytes);
			        m_pageIndex = render.request.pageIndex;
			        m_ui.set_page_image(slint::Image(std::move(pixels)));
			        m_ui.set_page_number(static_cast<int>(m_pageIndex + 1));
			        m_ui.set_loading(false);
		        });
	});
}

void PdfDemo::publishError(mecaps::pdf::DocumentError error)
{
	m_ui.set_loading(false);
	m_ui.set_error_message(slint::SharedString(errorMessage(error)));
}