#include "pdf_controller.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <list>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace mecaps::pdf {

class Controller::Impl
{
  public:
	Impl(std::unique_ptr<Backend> backend, Dispatcher dispatcher, ControllerLimits limits,
	        DispatchFailureReporter dispatchFailureReporter)
	    : m_backend(std::move(backend))
	    , m_dispatcher(std::move(dispatcher))
	    , m_dispatchFailureReporter(std::move(dispatchFailureReporter))
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
			if (m_activeDocument)
				m_activeDocument->cancel();
			m_visibleTasks.clear();
			m_prefetchTasks.clear();
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
		{
			const std::scoped_lock lock(m_mutex);
			if (!m_stopping && generation >= m_publication->generation.load(std::memory_order_relaxed)) {
				m_documentGeneration = generation;
				m_cache.clear();
				m_cacheBytes = 0;
			}
		}

		const auto sharedCallback = std::make_shared<OpenCallback>(std::move(callback));
		submit(generation, [generation, data = std::move(data), callback = sharedCallback](Impl &self) mutable {
			auto opened = self.m_backend->open(std::move(data));
			OpenResult result { generation, 0, opened.error };
			if (opened) {
				result.pageCount = opened.value->pageCount();
				if (result.pageCount > self.m_limits.maxPageCount) {
					result.error = DocumentError::resourceLimit;
				} else {
					self.m_document = std::move(opened.value);
					self.m_loadedDocumentGeneration = generation;
				}
			}
			if (result.error != DocumentError::none) {
				self.m_document.reset();
				self.m_loadedDocumentGeneration = 0;
			}
			return [callback = std::move(callback), result = std::move(result)]() mutable {
				(*callback)(std::move(result));
			};
		}, [generation, callback = std::move(sharedCallback)](DocumentError error) mutable -> Completion {
			return [generation, callback = std::move(callback), error]() mutable {
				(*callback)({ generation, 0, error });
			};
		});
	}

	void pageMetadata(GenerationId generation, std::size_t pageIndex, PageMetadataCallback callback)
	{
		const auto sharedCallback = std::make_shared<PageMetadataCallback>(std::move(callback));
		submit(generation, [generation, pageIndex, callback = sharedCallback](Impl &self) mutable {
			PageMetadataResult result { generation, pageIndex, {} };
			if (!self.m_document || self.m_loadedDocumentGeneration != self.documentGeneration()) {
				result.error = DocumentError::invalidDocument;
			} else {
				const auto metadata = self.m_document->pageMetadata(pageIndex);
				result.metadata = metadata.value;
				result.error = metadata.error;
				if (metadata && !self.validPageDimensions(metadata.value))
					result.error = DocumentError::resourceLimit;
			}
			return [callback = std::move(callback), result = std::move(result)]() mutable {
				(*callback)(std::move(result));
			};
		}, [generation, pageIndex, callback = std::move(sharedCallback)](DocumentError error) mutable -> Completion {
			return [generation, pageIndex, callback = std::move(callback), error]() mutable {
				(*callback)({ generation, pageIndex, {}, error });
			};
		});
	}

	void close(GenerationId generation)
	{
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
			m_documentGeneration = generation;
			m_cache.clear();
			m_cacheBytes = 0;
			m_closeRequested = true;
		}
		m_ready.notify_one();
	}

	void render(GenerationId generation, RenderRequest request, PixelFormat format, RenderCallback callback,
	        RenderPriority priority)
	{
		const auto pixelCount = static_cast<std::uint64_t>(request.outputWidth) * request.outputHeight;
		if (pixelCount > m_limits.maxOutputPixels) {
			fail(generation, [generation, request, format, callback = std::move(callback)]() mutable {
				callback({ generation, request, {}, 0, format, DocumentError::resourceLimit });
			});
			return;
		}

		std::optional<RenderResult> cachedResult;
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
			const CacheKey key { m_documentGeneration, request, format };
			const auto cached = std::find_if(m_cache.begin(), m_cache.end(),
			        [&key](const CacheEntry &entry) { return entry.key == key; });
			if (cached != m_cache.end()) {
				cachedResult = cached->result;
				cachedResult->generation = generation;
				m_cache.splice(m_cache.begin(), m_cache, cached);
			}
		}
		if (cachedResult) {
			dispatchOrReport(generation, [callback = std::move(callback), result = std::move(*cachedResult)]() mutable {
				callback(std::move(result));
			});
			return;
		}

		const auto sharedCallback = std::make_shared<RenderCallback>(std::move(callback));
		submit(generation, [generation, request, format, priority, callback = sharedCallback](Impl &self) mutable {
			RenderResult result { generation, request, {}, 0, format };
			if (!self.m_document || self.m_loadedDocumentGeneration != self.documentGeneration()) {
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
						auto pixels = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(byteCount));
						PixelBuffer buffer { *pixels, request.outputWidth, request.outputHeight,
							result.strideBytes, format };
						const bool mayRender = self.beginRender(generation, priority);
						result.error = mayRender ? self.m_document->render(request, buffer).error
						                         : DocumentError::cancelled;
						if (mayRender)
							self.endRender();
						if (result.error == DocumentError::none)
							result.pixels = std::move(pixels);
					}
				}
			}
			if (result.error == DocumentError::none)
				self.cache(generation, result);
			return [callback = std::move(callback), result = std::move(result)]() mutable {
				(*callback)(std::move(result));
			};
		}, [generation, request, format, callback = std::move(sharedCallback)](DocumentError error) mutable -> Completion {
			return [generation, request, format, callback = std::move(callback), error]() mutable {
				(*callback)({ generation, request, {}, 0, format, error });
			};
		}, priority);
	}

	void replaceGeneration(GenerationId generation)
	{
		const std::scoped_lock lock(m_mutex);
		if (!m_stopping)
			advanceGenerationLocked(generation);
	}

  private:
	struct PublicationState {
		std::atomic_bool alive { true };
		std::atomic<GenerationId> generation { 0 };
	};

	struct Task {
		GenerationId generation;
		std::function<Completion(Impl &)> execute;
		std::function<Completion(DocumentError)> failure;
		std::shared_ptr<std::atomic_bool> delivered;
	};

	struct CacheKey {
		GenerationId documentGeneration;
		RenderRequest request;
		PixelFormat format;

		bool operator==(const CacheKey &other) const noexcept
		{
			if (documentGeneration != other.documentGeneration
			    || request.pageIndex != other.request.pageIndex
			    || request.outputWidth != other.request.outputWidth
			    || request.outputHeight != other.request.outputHeight
			    || format != other.format)
				return false;
			if (request.outputWidth == 0 || request.outputHeight == 0)
				return false;

			// Approximate equality is intentionally used only by linear cache searches; it is not transitive.
			const double horizontalResolution = std::min(request.clip.width, other.request.clip.width)
			        / request.outputWidth;
			const double verticalResolution = std::min(request.clip.height, other.request.clip.height)
			        / request.outputHeight;
			const auto withinHalfPixel = [](double left, double right, double resolution) {
				return std::abs(left - right) <= resolution / 2.0;
			};
			return withinHalfPixel(request.clip.x, other.request.clip.x, horizontalResolution)
			        && withinHalfPixel(request.clip.y, other.request.clip.y, verticalResolution)
			        && withinHalfPixel(request.clip.width, other.request.clip.width, horizontalResolution)
			        && withinHalfPixel(request.clip.height, other.request.clip.height, verticalResolution);
		}
	};

	struct CacheEntry {
		CacheKey key;
		RenderResult result;
	};

	bool validPageDimensions(const PageMetadata &metadata) const noexcept
	{
		return std::isfinite(metadata.widthPoints) && std::isfinite(metadata.heightPoints)
		        && metadata.widthPoints > 0.0 && metadata.heightPoints > 0.0
		        && metadata.widthPoints <= m_limits.maxPageWidthPoints
		        && metadata.heightPoints <= m_limits.maxPageHeightPoints;
	}

	GenerationId documentGeneration()
	{
		const std::scoped_lock lock(m_mutex);
		return m_documentGeneration;
	}

	void advanceGenerationLocked(GenerationId generation)
	{
		if (generation <= m_publication->generation.load(std::memory_order_relaxed))
			return;
		if (m_activeDocument && m_activeRenderGeneration < generation)
			m_activeDocument->cancel();
		m_publication->generation.store(generation, std::memory_order_release);
		const auto oldSize = m_visibleTasks.size() + m_prefetchTasks.size();
		std::erase_if(m_visibleTasks, [generation](const Task &task) { return task.generation < generation; });
		std::erase_if(m_prefetchTasks, [generation](const Task &task) { return task.generation < generation; });
		const auto erasedCount = oldSize - m_visibleTasks.size() - m_prefetchTasks.size();
		assert(m_inFlight >= erasedCount);
		m_inFlight -= erasedCount;
	}

	void submit(GenerationId generation, std::function<Completion(Impl &)> execute,
	        std::function<Completion(DocumentError)> failure,
	        RenderPriority priority = RenderPriority::visible)
	{
		bool limitFailure = false;
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
			if (priority == RenderPriority::visible && m_activeDocument
			    && m_activeRenderPriority == RenderPriority::prefetch)
				m_activeDocument->cancel();
			const auto capacity = priority == RenderPriority::prefetch && m_limits.maxInFlightRequests > 0
			        ? m_limits.maxInFlightRequests - 1
			        : m_limits.maxInFlightRequests;
			if (m_inFlight >= capacity) {
				limitFailure = true;
			} else {
				auto &tasks = priority == RenderPriority::visible ? m_visibleTasks : m_prefetchTasks;
				tasks.push_back(
				        { generation, std::move(execute), std::move(failure), std::make_shared<std::atomic_bool>() });
				++m_inFlight;
			}
		}
		if (limitFailure) {
			dispatchOrReport(generation, failure(DocumentError::resourceLimit));
			return;
		}
		m_ready.notify_one();
	}

	bool beginRender(GenerationId generation, RenderPriority priority)
	{
		const std::scoped_lock lock(m_mutex);
		if (m_stopping || generation != m_publication->generation.load(std::memory_order_relaxed)
		    || (priority == RenderPriority::prefetch && !m_visibleTasks.empty()))
			return false;
		m_document->beginRender();
		m_activeDocument = m_document.get();
		m_activeRenderGeneration = generation;
		m_activeRenderPriority = priority;
		return true;
	}

	void endRender()
	{
		const std::scoped_lock lock(m_mutex);
		m_activeDocument = nullptr;
		m_activeRenderGeneration = 0;
	}

	void cache(GenerationId generation, const RenderResult &result)
	{
		const auto bytes = result.pixels->size();
		const std::scoped_lock lock(m_mutex);
		if (generation != m_publication->generation.load(std::memory_order_relaxed)
		    || bytes > m_limits.maxRasterCacheBytes)
			return;

		CacheKey key { m_documentGeneration, result.request, result.format };
		const auto existing = std::find_if(m_cache.begin(), m_cache.end(),
		        [&key](const CacheEntry &entry) { return entry.key == key; });
		if (existing != m_cache.end()) {
			m_cacheBytes -= existing->result.pixels->size();
			m_cache.erase(existing);
		}
		while (!m_cache.empty() && m_cacheBytes + bytes > m_limits.maxRasterCacheBytes) {
			m_cacheBytes -= m_cache.back().result.pixels->size();
			m_cache.pop_back();
		}
		m_cache.push_front({ std::move(key), result });
		m_cacheBytes += bytes;
	}

	void fail(GenerationId generation, Completion completion)
	{
		{
			const std::scoped_lock lock(m_mutex);
			if (m_stopping || generation < m_publication->generation.load(std::memory_order_relaxed))
				return;
			advanceGenerationLocked(generation);
		}
		dispatchOrReport(generation, std::move(completion));
	}

	bool finishTask(GenerationId generation)
	{
		const std::scoped_lock lock(m_mutex);
		if (m_inFlight > 0)
			--m_inFlight;
		return !m_stopping && generation == m_publication->generation.load(std::memory_order_relaxed);
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

	void dispatchOrReport(GenerationId generation, Completion completion) noexcept
	{
		try {
			dispatch(generation, std::move(completion));
		} catch (const std::bad_alloc &) {
			reportDispatchFailure(DocumentError::resourceLimit);
		} catch (...) {
			reportDispatchFailure(DocumentError::backendFailure);
		}
	}

	void dispatchFailure(Task &task, DocumentError error) noexcept
	{
		try {
			dispatch(task.generation, deliverOnce(task, task.failure(error)));
		} catch (const std::bad_alloc &) {
			reportDispatchFailure(DocumentError::resourceLimit);
		} catch (...) {
			reportDispatchFailure(DocumentError::backendFailure);
		}
	}

	void reportDispatchFailure(DocumentError error) noexcept
	{
		try {
			if (m_dispatchFailureReporter)
				m_dispatchFailureReporter(error);
		} catch (...) {
		}
	}

	Completion deliverOnce(const Task &task, Completion completion)
	{
		return [delivered = task.delivered, completion = std::move(completion)]() mutable {
			if (!delivered->exchange(true, std::memory_order_acq_rel))
				completion();
		};
	}

	void run()
	{
		for (;;) {
			Task task;
			{
				std::unique_lock lock(m_mutex);
				m_ready.wait(lock, [this] {
					return m_stopping || m_closeRequested || !m_visibleTasks.empty() || !m_prefetchTasks.empty();
				});
				if (m_stopping) {
					m_document.reset();
					m_backend.reset();
					return;
				}
				if (m_closeRequested) {
					m_document.reset();
					m_loadedDocumentGeneration = 0;
					m_closeRequested = false;
					continue;
				}
				auto &tasks = !m_visibleTasks.empty() ? m_visibleTasks : m_prefetchTasks;
				task = std::move(tasks.front());
				tasks.pop_front();
			}
			Completion completion;
			std::optional<DocumentError> error;
			try {
				completion = task.execute(*this);
			} catch (const std::bad_alloc &) {
				error = DocumentError::resourceLimit;
			} catch (...) {
				error = DocumentError::backendFailure;
			}

			if (!finishTask(task.generation))
				continue;
			if (error) {
				dispatchFailure(task, *error);
				continue;
			}
			try {
				dispatch(task.generation, deliverOnce(task, std::move(completion)));
			} catch (const std::bad_alloc &) {
				dispatchFailure(task, DocumentError::resourceLimit);
			} catch (...) {
				dispatchFailure(task, DocumentError::backendFailure);
			}
		}
	}

	std::unique_ptr<Backend> m_backend;
	std::unique_ptr<Document> m_document;
	Dispatcher m_dispatcher;
	DispatchFailureReporter m_dispatchFailureReporter;
	ControllerLimits m_limits;
	std::shared_ptr<PublicationState> m_publication = std::make_shared<PublicationState>();
	std::mutex m_mutex;
	std::condition_variable m_ready;
	std::deque<Task> m_visibleTasks;
	std::deque<Task> m_prefetchTasks;
	std::list<CacheEntry> m_cache;
	Document *m_activeDocument { nullptr };
	GenerationId m_documentGeneration { 0 };
	GenerationId m_loadedDocumentGeneration { 0 };
	GenerationId m_activeRenderGeneration { 0 };
	RenderPriority m_activeRenderPriority { RenderPriority::visible };
	std::size_t m_cacheBytes { 0 };
	std::size_t m_inFlight { 0 };
	bool m_closeRequested { false };
	bool m_stopping { false };
	std::thread m_worker;
};

Controller::Controller(std::unique_ptr<Backend> backend, Dispatcher dispatcher, ControllerLimits limits,
        DispatchFailureReporter dispatchFailureReporter)
	: m_impl(std::make_unique<Impl>(std::move(backend), std::move(dispatcher), limits,
	          std::move(dispatchFailureReporter)))
{
}

Controller::~Controller() = default;

void Controller::open(GenerationId generation, std::vector<std::byte> documentData, OpenCallback callback)
{
	m_impl->open(generation, std::move(documentData), std::move(callback));
}

void Controller::close(GenerationId generation)
{
	m_impl->close(generation);
}

void Controller::pageMetadata(GenerationId generation, std::size_t pageIndex, PageMetadataCallback callback)
{
	m_impl->pageMetadata(generation, pageIndex, std::move(callback));
}

void Controller::render(
        GenerationId generation,
        RenderRequest request,
        PixelFormat format,
	RenderCallback callback,
	RenderPriority priority)
{
	m_impl->render(generation, request, format, std::move(callback), priority);
}

void Controller::replaceGeneration(GenerationId generation)
{
	m_impl->replaceGeneration(generation);
}

} // namespace mecaps::pdf