#include "pdf_controller.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace mecaps::pdf {

class Controller::Impl
{
  public:
	Impl(std::unique_ptr<Backend> backend, Dispatcher dispatcher, ControllerLimits limits)
	    : m_backend(std::move(backend))
	    , m_dispatcher(std::move(dispatcher))
	    , m_limits(limits)
	    , m_worker([this] { run(); })
	{
	}

	~Impl()
	{
		m_publication->alive.store(false, std::memory_order_release);
		{
			const std::scoped_lock lock(m_mutex);
			m_stopping = true;
			m_tasks.clear();
			m_inFlight = 0;
		}
		m_ready.notify_one();
		if (m_worker.joinable())
			m_worker.join();
	}

	void open(GenerationId generation, std::vector<std::byte> data, OpenCallback callback)
	{
		if (data.size() > m_limits.maxDocumentBytes) {
			fail(generation, [generation, callback = std::move(callback)]() mutable {
				callback({ generation, 0, DocumentError::resourceLimit });
			});
			return;
		}

		auto limitCallback = callback;
		submit(generation, [generation, data = std::move(data), callback = std::move(callback)](Impl &self) mutable {
			auto opened = self.m_backend->open(std::move(data));
			OpenResult result { generation, 0, opened.error };
			if (opened) {
				result.pageCount = opened.value->pageCount();
				if (result.pageCount > self.m_limits.maxPageCount) {
					result.error = DocumentError::resourceLimit;
				} else {
					self.m_document = std::move(opened.value);
				}
			}
			if (result.error != DocumentError::none)
				self.m_document.reset();
			self.complete(generation, [callback = std::move(callback), result = std::move(result)]() mutable {
				callback(std::move(result));
			});
		}, [generation, callback = std::move(limitCallback)]() mutable {
			callback({ generation, 0, DocumentError::resourceLimit });
		});
	}

	void pageMetadata(GenerationId generation, std::size_t pageIndex, PageMetadataCallback callback)
	{
		auto limitCallback = callback;
		submit(generation, [generation, pageIndex, callback = std::move(callback)](Impl &self) mutable {
			PageMetadataResult result { generation, pageIndex, {} };
			if (!self.m_document) {
				result.error = DocumentError::invalidDocument;
			} else {
				const auto metadata = self.m_document->pageMetadata(pageIndex);
				result.metadata = metadata.value;
				result.error = metadata.error;
				if (metadata && !self.validPageDimensions(metadata.value))
					result.error = DocumentError::resourceLimit;
			}
			self.complete(generation, [callback = std::move(callback), result = std::move(result)]() mutable {
				callback(std::move(result));
			});
		}, [generation, pageIndex, callback = std::move(limitCallback)]() mutable {
			callback({ generation, pageIndex, {}, DocumentError::resourceLimit });
		});
	}

	void render(GenerationId generation, RenderRequest request, PixelFormat format, RenderCallback callback)
	{
		const auto pixelCount = static_cast<std::uint64_t>(request.outputWidth) * request.outputHeight;
		if (pixelCount > m_limits.maxOutputPixels) {
			fail(generation, [generation, request, format, callback = std::move(callback)]() mutable {
				callback({ generation, request, {}, 0, format, DocumentError::resourceLimit });
			});
			return;
		}

		auto limitCallback = callback;
		submit(generation, [generation, request, format, callback = std::move(callback)](Impl &self) mutable {
			RenderResult result { generation, request, {}, 0, format };
			if (!self.m_document) {
				result.error = DocumentError::invalidDocument;
			} else {
				const auto metadata = self.m_document->pageMetadata(request.pageIndex);
				if (!metadata) {
					result.error = metadata.error;
				} else if (!self.validPageDimensions(metadata.value)) {
					result.error = DocumentError::resourceLimit;
				} else {
					const auto pixelSize = static_cast<std::uint64_t>(bytesPerPixel(format));
					const auto stride = static_cast<std::uint64_t>(request.outputWidth) * pixelSize;
					const auto byteCount = stride * request.outputHeight;
					if (stride > std::numeric_limits<std::uint32_t>::max()) {
						result.error = DocumentError::resourceLimit;
					} else {
						result.strideBytes = static_cast<std::uint32_t>(stride);
						result.pixels.resize(static_cast<std::size_t>(byteCount));
						PixelBuffer buffer { result.pixels, request.outputWidth, request.outputHeight,
							result.strideBytes, format };
						result.error = self.m_document->render(request, buffer).error;
						if (result.error != DocumentError::none)
							result.pixels.clear();
					}
				}
			}
			self.complete(generation, [callback = std::move(callback), result = std::move(result)]() mutable {
				callback(std::move(result));
			});
		}, [generation, request, format, callback = std::move(limitCallback)]() mutable {
			callback({ generation, request, {}, 0, format, DocumentError::resourceLimit });
		});
	}

  private:
	struct PublicationState {
		std::atomic_bool alive { true };
		std::atomic<GenerationId> generation { 0 };
	};

	struct Task {
		GenerationId generation;
		std::function<void(Impl &)> execute;
	};

	bool validPageDimensions(const PageMetadata &metadata) const noexcept
	{
		return std::isfinite(metadata.widthPoints) && std::isfinite(metadata.heightPoints)
		        && metadata.widthPoints > 0.0 && metadata.heightPoints > 0.0
		        && metadata.widthPoints <= m_limits.maxPageWidthPoints
		        && metadata.heightPoints <= m_limits.maxPageHeightPoints;
	}

	void advanceGenerationLocked(GenerationId generation)
	{
		if (generation <= m_publication->generation.load(std::memory_order_relaxed))
			return;
		m_publication->generation.store(generation, std::memory_order_release);
		const auto oldSize = m_tasks.size();
		std::erase_if(m_tasks, [generation](const Task &task) { return task.generation < generation; });
		m_inFlight -= oldSize - m_tasks.size();
	}

	void submit(GenerationId generation, std::function<void(Impl &)> execute, Completion limitCompletion)
	{
		bool limitFailure = false;
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
			if (m_inFlight >= m_limits.maxInFlightRequests) {
				limitFailure = true;
			} else {
				m_tasks.push_back({ generation, std::move(execute) });
				++m_inFlight;
			}
		}
		if (limitFailure) {
			dispatch(generation, std::move(limitCompletion));
			return;
		}
		m_ready.notify_one();
	}

	void fail(GenerationId generation, Completion completion)
	{
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
		}
		dispatch(generation, std::move(completion));
	}

	void complete(GenerationId generation, Completion completion)
	{
		{
			const std::scoped_lock lock(m_mutex);
			if (m_inFlight > 0)
				--m_inFlight;
			if (m_stopping || generation != m_publication->generation.load(std::memory_order_relaxed))
				return;
		}
		dispatch(generation, std::move(completion));
	}

	void dispatch(GenerationId generation, Completion completion)
	{
		const std::weak_ptr<PublicationState> weakPublication = m_publication;
		m_dispatcher([weakPublication, generation, completion = std::move(completion)]() mutable {
			const auto publication = weakPublication.lock();
			if (publication && publication->alive.load(std::memory_order_acquire)
			    && publication->generation.load(std::memory_order_acquire) == generation)
				completion();
		});
	}

	void run()
	{
		for (;;) {
			Task task;
			{
				std::unique_lock lock(m_mutex);
				m_ready.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
				if (m_stopping) {
					m_document.reset();
					m_backend.reset();
					return;
				}
				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}
			task.execute(*this);
		}
	}

	std::unique_ptr<Backend> m_backend;
	std::unique_ptr<Document> m_document;
	Dispatcher m_dispatcher;
	ControllerLimits m_limits;
	std::shared_ptr<PublicationState> m_publication = std::make_shared<PublicationState>();
	std::mutex m_mutex;
	std::condition_variable m_ready;
	std::deque<Task> m_tasks;
	std::size_t m_inFlight { 0 };
	bool m_stopping { false };
	std::thread m_worker;
};

Controller::Controller(std::unique_ptr<Backend> backend, Dispatcher dispatcher, ControllerLimits limits)
    : m_impl(std::make_unique<Impl>(std::move(backend), std::move(dispatcher), limits))
{
}

Controller::~Controller() = default;

void Controller::open(GenerationId generation, std::vector<std::byte> documentData, OpenCallback callback)
{
	m_impl->open(generation, std::move(documentData), std::move(callback));
}

void Controller::pageMetadata(GenerationId generation, std::size_t pageIndex, PageMetadataCallback callback)
{
	m_impl->pageMetadata(generation, pageIndex, std::move(callback));
}

void Controller::render(
        GenerationId generation,
        RenderRequest request,
        PixelFormat format,
        RenderCallback callback)
{
	m_impl->render(generation, request, format, std::move(callback));
}

} // namespace mecaps::pdf