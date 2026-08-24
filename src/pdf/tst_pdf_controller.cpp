#include "pdf_controller.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

using namespace mecaps::pdf;

class TestDispatcher
{
  public:
	enum class Failure {
		none,
		badAllocation,
		other,
		enqueueThenThrow,
		invokeThenThrow,
	};

	Dispatcher dispatcher()
	{
		return [this](Completion completion) {
			const std::scoped_lock lock(m_mutex);
			if (m_failAllDispatches)
				throw std::runtime_error("persistent dispatcher failure");
			const auto failure = std::exchange(m_failure, Failure::none);
			if (failure == Failure::badAllocation)
				throw std::bad_alloc {};
			if (failure == Failure::other)
				throw std::runtime_error("dispatcher failure");
			m_completions.push_back(std::move(completion));
			m_ready.notify_all();
			if (failure == Failure::enqueueThenThrow)
				throw std::runtime_error("dispatcher failure after enqueue");
			if (failure == Failure::invokeThenThrow) {
				auto invoked = std::move(m_completions.back());
				m_completions.pop_back();
				invoked();
				throw std::runtime_error("dispatcher failure after invocation");
			}
		};
	}

	void failNextDispatch(Failure failure)
	{
		const std::scoped_lock lock(m_mutex);
		m_failure = failure;
	}

	void failAllDispatches()
	{
		const std::scoped_lock lock(m_mutex);
		m_failAllDispatches = true;
	}

	void drain(std::size_t count = 1)
	{
		std::unique_lock lock(m_mutex);
		m_ready.wait(lock, [this, count] { return m_completions.size() >= count; });
		auto completions = std::move(m_completions);
		m_completions.clear();
		lock.unlock();
		for (auto &completion : completions)
			completion();
	}

	std::size_t pending() const
	{
		const std::scoped_lock lock(m_mutex);
		return m_completions.size();
	}

  private:
	mutable std::mutex m_mutex;
	std::condition_variable m_ready;
	std::deque<Completion> m_completions;
	Failure m_failure { Failure::none };
	bool m_failAllDispatches { false };
};

struct FakeState {
	std::mutex mutex;
	std::condition_variable ready;
	std::thread::id backendThread;
	std::thread::id destructionThread;
	const std::byte *openedData { nullptr };
	bool renderEntered { false };
	bool releaseRender { true };
	bool cancelled { false };
	std::size_t cancelCount { 0 };
	std::size_t renderCount { 0 };
	std::vector<std::size_t> renderedPages;
};

class FakeDocument final : public Document
{
  public:
	explicit FakeDocument(std::shared_ptr<FakeState> state, std::size_t pageCount = 2, PageMetadata page = { 612, 792 })
	    : m_state(std::move(state))
	    , m_pageCount(pageCount)
	    , m_page(page)
	{
	}

	~FakeDocument() override
	{
		const std::scoped_lock lock(m_state->mutex);
		m_state->destructionThread = std::this_thread::get_id();
	}

	std::size_t pageCount() const noexcept override
	{
		recordThread();
		return m_pageCount;
	}

	void beginRender() noexcept override
	{
		const std::scoped_lock lock(m_state->mutex);
		m_state->cancelled = false;
	}

	Result<PageMetadata> pageMetadata(std::size_t pageIndex) const noexcept override
	{
		recordThread();
		return pageIndex < m_pageCount ? Result<PageMetadata> { m_page }
		                               : Result<PageMetadata> { {}, DocumentError::pageOutOfBounds };
	}

	Result<void> render(const RenderRequest &request, PixelBuffer buffer) noexcept override
	{
		recordThread();
		std::unique_lock lock(m_state->mutex);
		++m_state->renderCount;
		m_state->renderedPages.push_back(request.pageIndex);
		m_state->renderEntered = true;
		m_state->ready.notify_all();
		m_state->ready.wait(lock, [this] { return m_state->releaseRender; });
		if (m_state->cancelled) {
			m_state->cancelled = false;
			return { DocumentError::cancelled };
		}
		if (!buffer.pixels.empty())
			buffer.pixels.front() = std::byte { 0x2a };
		return {};
	}

	void cancel() noexcept override
	{
		const std::scoped_lock lock(m_state->mutex);
		m_state->cancelled = true;
		m_state->releaseRender = true;
		++m_state->cancelCount;
		m_state->ready.notify_all();
	}

  private:
	void recordThread() const noexcept
	{
		const std::scoped_lock lock(m_state->mutex);
		m_state->backendThread = std::this_thread::get_id();
	}

	std::shared_ptr<FakeState> m_state;
	std::size_t m_pageCount;
	PageMetadata m_page;
};

class FakeBackend final : public Backend
{
  public:
	FakeBackend(std::shared_ptr<FakeState> state, std::size_t pageCount = 2, PageMetadata page = { 612, 792 })
	    : m_state(std::move(state))
	    , m_pageCount(pageCount)
	    , m_page(page)
	{
	}

	Result<std::unique_ptr<Document>> open(std::vector<std::byte> data) noexcept override
	{
		{
			const std::scoped_lock lock(m_state->mutex);
			m_state->backendThread = std::this_thread::get_id();
			m_state->openedData = data.data();
		}
		if (data.empty())
			return { nullptr, DocumentError::invalidDocument };
		return { std::make_unique<FakeDocument>(m_state, m_pageCount, m_page) };
	}

  private:
	std::shared_ptr<FakeState> m_state;
	std::size_t m_pageCount;
	PageMetadata m_page;
};

RenderRequest request()
{
	return { 0, { 0, 0, 612, 792 }, 4, 4 };
}

TEST_SUITE("PDF controller")
{
	TEST_CASE("backend work is confined to a worker and completion uses the dispatcher")
	{
		const auto callerThread = std::this_thread::get_id();
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		bool completed = false;
		{
			Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
			std::vector data { std::byte { 1 } };
			const auto *dataAddress = data.data();
			controller.open(1, std::move(data),
			        [&](OpenResult result) { completed = result.error == DocumentError::none; });
			dispatcher.drain();
			CHECK(state->openedData == dataAddress);
		}

		CHECK(completed);
		CHECK(state->backendThread != callerThread);
		CHECK(state->destructionThread == state->backendThread);
	}

	TEST_CASE("a replacement suppresses an active stale render result")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		{
			const std::scoped_lock lock(state->mutex);
			state->releaseRender = false;
		}
		std::vector<GenerationId> published;
		controller.render(2, request(), PixelFormat::rgba8,
		        [&](RenderResult result) { published.push_back(result.generation); });
		{
			std::unique_lock lock(state->mutex);
			state->ready.wait(lock, [&] { return state->renderEntered; });
		}
		controller.render(3, request(), PixelFormat::rgba8,
		        [&](RenderResult result) { published.push_back(result.generation); });
		dispatcher.drain();

		CHECK(state->cancelCount == 1);
		CHECK(published == std::vector<GenerationId> { 3 });
	}

	TEST_CASE("a completion already queued in the dispatcher becomes stale")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		std::vector<GenerationId> published;
		controller.open(1, { std::byte { 1 } },
		        [&](OpenResult result) { published.push_back(result.generation); });
		while (dispatcher.pending() == 0)
			std::this_thread::yield();

		controller.pageMetadata(2, 0,
		        [&](PageMetadataResult result) { published.push_back(result.generation); });
		dispatcher.drain(2);

		CHECK(published == std::vector<GenerationId> { 2 });
	}

	TEST_CASE("closing clears the current document on the worker")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		const auto callerThread = std::this_thread::get_id();
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		controller.close(2);
		DocumentError error = DocumentError::none;
		controller.pageMetadata(3, 0, [&](PageMetadataResult result) { error = result.error; });
		dispatcher.drain();

		CHECK(error == DocumentError::invalidDocument);
		CHECK(state->destructionThread == state->backendThread);
		CHECK(state->destructionThread != callerThread);
	}

	TEST_CASE("generation replacement suppresses a result before delayed work is submitted")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		bool published = false;
		controller.render(2, request(), PixelFormat::rgba8, [&](RenderResult) { published = true; });
		while (dispatcher.pending() == 0)
			std::this_thread::yield();
		controller.replaceGeneration(3);
		dispatcher.drain();

		CHECK_FALSE(published);
	}

	TEST_CASE("queued completion cannot run after shutdown")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		bool completed = false;
		{
			Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
			controller.open(1, { std::byte { 1 } }, [&](OpenResult) { completed = true; });
			dispatcher.drain();
			CHECK(completed);
			completed = false;
			controller.pageMetadata(2, 0, [&](PageMetadataResult) { completed = true; });
			while (dispatcher.pending() == 0)
				std::this_thread::yield();
		}
		dispatcher.drain();
		CHECK_FALSE(completed);
	}

	TEST_CASE("worker exceptions are reported and do not stop later work")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		SUBCASE("allocation failure")
		{
			dispatcher.failNextDispatch(TestDispatcher::Failure::badAllocation);
			DocumentError error = DocumentError::none;
			std::size_t callbackCount = 0;
			controller.pageMetadata(2, 0, [&](PageMetadataResult result) {
				++callbackCount;
				error = result.error;
			});
			dispatcher.drain();
			CHECK(callbackCount == 1);
			CHECK(error == DocumentError::resourceLimit);
		}

		SUBCASE("unexpected failure")
		{
			dispatcher.failNextDispatch(TestDispatcher::Failure::other);
			DocumentError error = DocumentError::none;
			std::size_t callbackCount = 0;
			controller.pageMetadata(2, 0, [&](PageMetadataResult result) {
				++callbackCount;
				error = result.error;
			});
			dispatcher.drain();
			CHECK(callbackCount == 1);
			CHECK(error == DocumentError::backendFailure);
		}

		SUBCASE("failure after enqueue")
		{
			dispatcher.failNextDispatch(TestDispatcher::Failure::enqueueThenThrow);
			DocumentError error = DocumentError::backendFailure;
			std::size_t callbackCount = 0;
			controller.pageMetadata(2, 0, [&](PageMetadataResult result) {
				++callbackCount;
				error = result.error;
			});
			dispatcher.drain(2);
			CHECK(callbackCount == 1);
			CHECK(error == DocumentError::none);
		}

		SUBCASE("failure after synchronous invocation")
		{
			dispatcher.failNextDispatch(TestDispatcher::Failure::invokeThenThrow);
			DocumentError error = DocumentError::backendFailure;
			std::size_t callbackCount = 0;
			controller.pageMetadata(2, 0, [&](PageMetadataResult result) {
				++callbackCount;
				error = result.error;
			});
			dispatcher.drain();
			CHECK(callbackCount == 1);
			CHECK(error == DocumentError::none);
		}

		DocumentError laterError = DocumentError::backendFailure;
		controller.pageMetadata(3, 0, [&](PageMetadataResult result) { laterError = result.error; });
		dispatcher.drain();
		CHECK(laterError == DocumentError::none);
	}

	TEST_CASE("a persistent dispatcher failure is reported to the host")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		std::promise<DocumentError> reportedError;
		auto reported = reportedError.get_future();
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), {},
		        [&](DocumentError error) { reportedError.set_value(error); });
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		dispatcher.failAllDispatches();
		controller.pageMetadata(2, 0, [](PageMetadataResult) {});

		REQUIRE(reported.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
		CHECK(reported.get() == DocumentError::backendFailure);
	}

	TEST_CASE("caller-thread dispatcher failures are reported to the host")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		std::vector<DocumentError> reported;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), {},
		        [&](DocumentError error) { reported.push_back(error); });

		SUBCASE("immediate failure")
		{
			dispatcher.failNextDispatch(TestDispatcher::Failure::badAllocation);
			auto oversizedRequest = request();
			oversizedRequest.outputWidth = 5'000;
			oversizedRequest.outputHeight = 5'000;
			controller.render(1, oversizedRequest, PixelFormat::rgba8, [](RenderResult) {});
			CHECK(reported == std::vector { DocumentError::resourceLimit });
		}

		SUBCASE("cache hit")
		{
			controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
			dispatcher.drain();
			controller.render(2, request(), PixelFormat::rgba8, [](RenderResult) {});
			dispatcher.drain();

			dispatcher.failNextDispatch(TestDispatcher::Failure::other);
			controller.render(3, request(), PixelFormat::rgba8, [](RenderResult) {});
			CHECK(reported == std::vector { DocumentError::backendFailure });
		}
	}

	TEST_CASE("controller boundaries report configured resource limits")
	{
		SUBCASE("document bytes")
		{
			auto state = std::make_shared<FakeState>();
			TestDispatcher dispatcher;
			ControllerLimits limits;
			limits.maxDocumentBytes = 1;
			Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), limits);
			DocumentError error = DocumentError::none;
			controller.open(1, { std::byte { 1 }, std::byte { 2 } }, [&](OpenResult result) { error = result.error; });
			dispatcher.drain();
			CHECK(error == DocumentError::resourceLimit);
		}

		SUBCASE("page count")
		{
			auto state = std::make_shared<FakeState>();
			TestDispatcher dispatcher;
			ControllerLimits limits;
			limits.maxPageCount = 1;
			Controller controller(std::make_unique<FakeBackend>(state, 2), dispatcher.dispatcher(), limits);
			DocumentError error = DocumentError::none;
			controller.open(1, { std::byte { 1 } }, [&](OpenResult result) { error = result.error; });
			dispatcher.drain();
			CHECK(error == DocumentError::resourceLimit);
		}

		SUBCASE("page dimensions and output pixels")
		{
			auto state = std::make_shared<FakeState>();
			TestDispatcher dispatcher;
			ControllerLimits limits;
			limits.maxPageWidthPoints = 100;
			limits.maxOutputPixels = 4;
			Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), limits);
			controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
			dispatcher.drain();
			DocumentError metadataError = DocumentError::none;
			controller.pageMetadata(2, 0, [&](PageMetadataResult result) { metadataError = result.error; });
			dispatcher.drain();
			CHECK(metadataError == DocumentError::resourceLimit);
			DocumentError renderError = DocumentError::none;
			controller.render(3, request(), PixelFormat::rgba8,
			        [&](RenderResult result) { renderError = result.error; });
			dispatcher.drain();
			CHECK(renderError == DocumentError::resourceLimit);
		}

		SUBCASE("in-flight requests")
		{
			auto state = std::make_shared<FakeState>();
			TestDispatcher dispatcher;
			ControllerLimits limits;
			limits.maxInFlightRequests = 0;
			Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), limits);
			DocumentError error = DocumentError::none;
			controller.open(1, { std::byte { 1 } }, [&](OpenResult result) { error = result.error; });
			dispatcher.drain();
			CHECK(error == DocumentError::resourceLimit);
		}
	}

	TEST_CASE("exact renders are cached and the key separates render parameters")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		std::shared_ptr<const std::vector<std::byte>> firstPixels;
		auto render = [&](GenerationId generation, RenderRequest renderRequest, PixelFormat format) {
			controller.render(generation, renderRequest, format, [&](RenderResult result) {
				if (!firstPixels)
					firstPixels = result.pixels;
				else if (generation == 3)
					CHECK(result.pixels == firstPixels);
			});
			dispatcher.drain();
		};
		render(2, request(), PixelFormat::rgba8);
		render(3, request(), PixelFormat::rgba8);
		CHECK(state->renderCount == 1);

		auto equivalentClip = request();
		equivalentClip.clip.x = std::nextafter(equivalentClip.clip.x, 1.0);
		equivalentClip.clip.width = std::nextafter(equivalentClip.clip.width, 0.0);
		render(4, equivalentClip, PixelFormat::rgba8);
		CHECK(state->renderCount == 1);

		auto differentClip = request();
		differentClip.clip.x += differentClip.clip.width / differentClip.outputWidth;
		differentClip.clip.width -= differentClip.clip.width / differentClip.outputWidth;
		render(5, differentClip, PixelFormat::rgba8);
		auto differentSize = request();
		differentSize.outputWidth = 5;
		render(6, differentSize, PixelFormat::rgba8);
		render(7, request(), PixelFormat::rgb8);
		CHECK(state->renderCount == 4);
	}

	TEST_CASE("an oversized replacement preserves the current document cache")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		ControllerLimits limits;
		limits.maxDocumentBytes = 1;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher(), limits);
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		std::shared_ptr<const std::vector<std::byte>> cachedPixels;
		controller.render(2, request(), PixelFormat::rgba8,
		        [&](RenderResult result) { cachedPixels = std::move(result.pixels); });
		dispatcher.drain();

		DocumentError openError = DocumentError::none;
		controller.open(3, { std::byte { 1 }, std::byte { 2 } },
		        [&](OpenResult result) { openError = result.error; });
		dispatcher.drain();
		std::shared_ptr<const std::vector<std::byte>> reusedPixels;
		controller.render(4, request(), PixelFormat::rgba8,
		        [&](RenderResult result) { reusedPixels = std::move(result.pixels); });
		dispatcher.drain();

		CHECK(openError == DocumentError::resourceLimit);
		CHECK(reusedPixels == cachedPixels);
		CHECK(state->renderCount == 1);
	}

	TEST_CASE("raster cache is byte bounded and evicts the least recently used entry")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		ControllerLimits limits;
		limits.maxRasterCacheBytes = 128;
		Controller controller(std::make_unique<FakeBackend>(state, 3), dispatcher.dispatcher(), limits);
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();

		auto renderPage = [&](GenerationId generation, std::size_t pageIndex) {
			auto renderRequest = request();
			renderRequest.pageIndex = pageIndex;
			controller.render(generation, renderRequest, PixelFormat::rgba8, [](RenderResult) {});
			dispatcher.drain();
		};
		renderPage(2, 0);
		renderPage(3, 1);
		renderPage(4, 0);
		renderPage(5, 2);
		renderPage(6, 1);
		CHECK(state->renderCount == 4);
	}

	TEST_CASE("opening another document rejects cached renders from the previous document")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();
		controller.render(2, request(), PixelFormat::rgba8, [](RenderResult) {});
		dispatcher.drain();
		controller.open(3, { std::byte { 2 } }, [](OpenResult) {});
		dispatcher.drain();
		controller.render(4, request(), PixelFormat::rgba8, [](RenderResult) {});
		dispatcher.drain();
		CHECK(state->renderCount == 2);
	}

	TEST_CASE("a rejected replacement document cannot expose the previous document")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();
		controller.render(2, request(), PixelFormat::rgba8, [](RenderResult) {});
		dispatcher.drain();
		controller.open(3, {}, [](OpenResult) {});
		dispatcher.drain();
		DocumentError error = DocumentError::none;
		controller.render(4, request(), PixelFormat::rgba8,
		        [&](RenderResult result) { error = result.error; });
		dispatcher.drain();
		CHECK(error == DocumentError::invalidDocument);
		CHECK(state->renderCount == 1);
	}

	TEST_CASE("visible work runs before queued prefetch work")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state, 4), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();
		{
			const std::scoped_lock lock(state->mutex);
			state->releaseRender = false;
		}
		auto submit = [&](std::size_t pageIndex, RenderPriority priority) {
			auto renderRequest = request();
			renderRequest.pageIndex = pageIndex;
			controller.render(2, renderRequest, PixelFormat::rgba8, [](RenderResult) {}, priority);
		};
		submit(0, RenderPriority::visible);
		{
			std::unique_lock lock(state->mutex);
			state->ready.wait(lock, [&] { return state->renderEntered; });
		}
		submit(1, RenderPriority::prefetch);
		submit(2, RenderPriority::prefetch);
		submit(3, RenderPriority::visible);
		{
			const std::scoped_lock lock(state->mutex);
			state->releaseRender = true;
			state->ready.notify_all();
		}
		dispatcher.drain(4);
		CHECK(state->renderedPages == std::vector<std::size_t> { 0, 3, 1, 2 });
	}

	TEST_CASE("visible work cancels an active prefetch render")
	{
		auto state = std::make_shared<FakeState>();
		TestDispatcher dispatcher;
		Controller controller(std::make_unique<FakeBackend>(state), dispatcher.dispatcher());
		controller.open(1, { std::byte { 1 } }, [](OpenResult) {});
		dispatcher.drain();
		{
			const std::scoped_lock lock(state->mutex);
			state->releaseRender = false;
		}
		controller.render(2, request(), PixelFormat::rgba8, [](RenderResult) {}, RenderPriority::prefetch);
		{
			std::unique_lock lock(state->mutex);
			state->ready.wait(lock, [&] { return state->renderEntered; });
		}
		auto visibleRequest = request();
		visibleRequest.pageIndex = 1;
		bool visibleCompleted = false;
		controller.render(2, visibleRequest, PixelFormat::rgba8,
		        [&](RenderResult result) { visibleCompleted = result.error == DocumentError::none; });
		dispatcher.drain(2);
		CHECK(state->cancelCount == 1);
		CHECK(visibleCompleted);
		CHECK(state->renderedPages == std::vector<std::size_t> { 0, 1 });
	}
}

} // namespace