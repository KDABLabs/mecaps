#include "pdf_controller.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace {

using namespace mecaps::pdf;

class TestDispatcher
{
  public:
	Dispatcher dispatcher()
	{
		return [this](Completion completion) {
			const std::scoped_lock lock(m_mutex);
			m_completions.push_back(std::move(completion));
			m_ready.notify_all();
		};
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
};

struct FakeState {
	std::mutex mutex;
	std::condition_variable ready;
	std::thread::id backendThread;
	std::thread::id destructionThread;
	const std::byte *openedData { nullptr };
	bool renderEntered { false };
	bool releaseRender { true };
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

	Result<PageMetadata> pageMetadata(std::size_t pageIndex) const noexcept override
	{
		recordThread();
		return pageIndex < m_pageCount ? Result<PageMetadata> { m_page }
		                               : Result<PageMetadata> { {}, DocumentError::pageOutOfBounds };
	}

	Result<void> render(const RenderRequest &, PixelBuffer buffer) noexcept override
	{
		recordThread();
		std::unique_lock lock(m_state->mutex);
		m_state->renderEntered = true;
		m_state->ready.notify_all();
		m_state->ready.wait(lock, [this] { return m_state->releaseRender; });
		if (!buffer.pixels.empty())
			buffer.pixels.front() = std::byte { 0x2a };
		return {};
	}

	void cancel() noexcept override { recordThread(); }

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
		{
			const std::scoped_lock lock(state->mutex);
			state->releaseRender = true;
			state->ready.notify_all();
		}
		dispatcher.drain();

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
}

} // namespace