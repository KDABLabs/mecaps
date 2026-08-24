#include "gstreamer_video_player.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <gst/gst.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

using namespace mecaps::video;

namespace {

class TestDispatcher
{
  public:
	GStreamerVideoPlayer::Dispatcher dispatcher()
	{
		return [this](std::function<void()> notification) {
			{
				std::scoped_lock lock(m_mutex);
				m_notifications.push_back(std::move(notification));
				++m_notificationCount;
			}
			m_changed.notify_one();
		};
	}

	std::size_t notificationCount()
	{
		std::scoped_lock lock(m_mutex);
		return m_notificationCount;
	}

	template <typename Predicate>
	bool waitFor(Predicate predicate, std::chrono::milliseconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!predicate()) {
			std::function<void()> notification;
			{
				std::unique_lock lock(m_mutex);
				if (!m_changed.wait_until(lock, deadline, [this] {
						return !m_notifications.empty();
					}))
					return predicate();
				notification = std::move(m_notifications.front());
				m_notifications.pop_front();
			}
			notification();
		}
		return true;
	}

  private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::deque<std::function<void()>> m_notifications;
	std::size_t m_notificationCount = 0;
};

class FatalCriticalGuard
{
  public:
	FatalCriticalGuard()
		: m_previousMask(g_log_set_always_fatal(static_cast<GLogLevelFlags>(
			  G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL)))
	{
	}

	~FatalCriticalGuard() { g_log_set_always_fatal(m_previousMask); }

  private:
	GLogLevelFlags m_previousMask;
};

class TemporaryMediaFile
{
  public:
	TemporaryMediaFile()
	{
		static std::atomic_uint64_t nextId = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("mecaps-video-" + std::to_string(getpid()) + "-" +
				  std::to_string(nextId.fetch_add(1)) + ".ogv");
	}

	~TemporaryMediaFile()
	{
		std::error_code error;
		std::filesystem::remove(m_path, error);
	}

	const std::filesystem::path &path() const { return m_path; }

  private:
	std::filesystem::path m_path;
};

void generateTestMedia(const std::filesystem::path &path, std::uint32_t width,
					   std::uint32_t height)
{
	const auto pipelineDescription =
		std::string("videotestsrc num-buffers=60 ! ") +
		"video/x-raw,width=" + std::to_string(width) +
		",height=" + std::to_string(height) +
		",framerate=30/1 ! theoraenc ! oggmux ! filesink location=" +
		path.string();
	GError *parseError = nullptr;
	GstElement *generator =
		gst_parse_launch(pipelineDescription.c_str(), &parseError);
	const std::string generatorError =
		parseError ? parseError->message : "Could not create media generator";
	INFO(generatorError);
	REQUIRE(generator != nullptr);
	if (parseError)
		g_error_free(parseError);
	GstBus *generatorBus = gst_element_get_bus(generator);
	REQUIRE(gst_element_set_state(generator, GST_STATE_PLAYING) !=
			GST_STATE_CHANGE_FAILURE);
	GstMessage *terminalMessage = gst_bus_timed_pop_filtered(
		generatorBus, 10 * GST_SECOND,
		static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
	REQUIRE(terminalMessage != nullptr);
	CHECK(GST_MESSAGE_TYPE(terminalMessage) == GST_MESSAGE_EOS);
	gst_message_unref(terminalMessage);
	gst_element_set_state(generator, GST_STATE_NULL);
	gst_object_unref(generatorBus);
	gst_object_unref(generator);
}

} // namespace

TEST_CASE("GStreamer player requires an owner-thread dispatcher")
{
	CHECK_THROWS_AS(GStreamerVideoPlayer(GStreamerVideoPlayer::Dispatcher{}),
					std::invalid_argument);
}

TEST_CASE("unavailable GStreamer factories reject every command")
{
	FatalCriticalGuard fatalCriticalGuard;
	GStreamerVideoPlayer::FactoryNames factoryNames;
	SUBCASE("pipeline factory unavailable")
	{
		factoryNames.pipeline = "mecaps-missing-playbin";
	}
	SUBCASE("sink factory unavailable")
	{
		factoryNames.sink = "mecaps-missing-appsink";
	}

	TestDispatcher dispatcher;
	std::vector<CommandCompletion> completions;
	GStreamerVideoPlayer player(dispatcher.dispatcher(),
								std::move(factoryNames));
	player.commandCompleted
		.connect([&](const CommandCompletion &completion) {
			completions.push_back(completion);
		})
		.release();

	std::vector<std::uint64_t> commandIds;
	commandIds.push_back(player.open("missing.webm"));
	commandIds.push_back(player.play());
	commandIds.push_back(player.pause());
	commandIds.push_back(player.stop());
	commandIds.push_back(player.seek(std::chrono::milliseconds(10)));
	commandIds.push_back(player.setVolume(0.5));
	commandIds.push_back(player.setMuted(true));
	REQUIRE(dispatcher.waitFor(
		[&] { return completions.size() == commandIds.size(); },
		std::chrono::seconds(2)));

	REQUIRE(completions.size() == commandIds.size());
	for (std::size_t index = 0; index < completions.size(); ++index) {
		CHECK(completions[index].commandId == commandIds[index]);
		CHECK_FALSE(completions[index].succeeded);
		CHECK(completions[index].error ==
			  "GStreamer playbin or appsink plugin is unavailable");
	}
	CHECK(player.error.get() ==
		  "GStreamer playbin or appsink plugin is unavailable");
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

TEST_CASE("invalid local source reports failed command completion")
{
	TestDispatcher dispatcher;
	std::vector<CommandCompletion> results;
	const auto ownerThread = std::this_thread::get_id();
	bool callbackOnOwnerThread = false;
	GStreamerVideoPlayer player(dispatcher.dispatcher());
	player.commandCompleted
		.connect([&](const CommandCompletion &completion) {
			callbackOnOwnerThread = std::this_thread::get_id() == ownerThread;
			results.push_back(completion);
		})
		.release();

	const auto commandId = player.open("/path/that/does/not/exist.ogv");
	REQUIRE(dispatcher.waitFor([&] { return !results.empty(); },
							   std::chrono::seconds(2)));
	CHECK(callbackOnOwnerThread);
	REQUIRE(results.size() == 1);
	CHECK(results[0].commandId == commandId);
	CHECK(results[0].type == CommandType::Open);
	CHECK_FALSE(results[0].succeeded);
	CHECK(results[0].error == "Source is not a local file");
	CHECK(player.error.get() == results[0].error);
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	CHECK(results.size() == 1);
}

TEST_CASE("GStreamer player can be destroyed from a command callback")
{
	TestDispatcher dispatcher;
	auto player =
		std::make_unique<GStreamerVideoPlayer>(dispatcher.dispatcher());
	bool destroyed = false;
	player->commandCompleted
		.connect([&](const CommandCompletion &) {
			player.reset();
			destroyed = true;
		})
		.release();

	player->open("/path/that/does/not/exist.ogv");
	REQUIRE(
		dispatcher.waitFor([&] { return destroyed; }, std::chrono::seconds(2)));
	CHECK(player == nullptr);
}

TEST_CASE("GStreamer player can be destroyed from a property callback")
{
	TestDispatcher dispatcher;
	auto player =
		std::make_unique<GStreamerVideoPlayer>(dispatcher.dispatcher());
	bool destroyed = false;
	player->error.valueChanged()
		.connect([&](const std::string &) {
			player.reset();
			destroyed = true;
		})
		.release();

	player->open("/path/that/does/not/exist.ogv");
	REQUIRE(
		dispatcher.waitFor([&] { return destroyed; }, std::chrono::seconds(2)));
	CHECK(player == nullptr);
}

TEST_CASE("timed out state command releases the command queue")
{
	gst_init(nullptr, nullptr);
	TemporaryMediaFile mediaFile;
	generateTestMedia(mediaFile.path(), 160, 90);

	TestDispatcher dispatcher;
	std::vector<CommandCompletion> completions;
	GStreamerVideoPlayer player(dispatcher.dispatcher(), {},
								std::chrono::milliseconds::zero());
	player.commandCompleted
		.connect([&](const CommandCompletion &completion) {
			completions.push_back(completion);
		})
		.release();

	const auto openCommand = player.open(mediaFile.path().string());
	REQUIRE(dispatcher.waitFor(
		[&] {
			return std::ranges::any_of(
				completions, [&](const CommandCompletion &completion) {
					return completion.commandId == openCommand;
				});
		},
		std::chrono::seconds(2)));
	const auto openCompletion = std::ranges::find_if(
		completions, [&](const CommandCompletion &completion) {
			return completion.commandId == openCommand;
		});
	REQUIRE(openCompletion != completions.end());
	CHECK_FALSE(openCompletion->succeeded);
	CHECK(openCompletion->error == "GStreamer state command timed out");
	CHECK(player.state.get() == PlaybackState::Stopped);
	CHECK(player.error.get() == openCompletion->error);

	const auto stopCommand = player.stop();
	REQUIRE(dispatcher.waitFor(
		[&] {
			return std::ranges::any_of(
				completions, [&](const CommandCompletion &completion) {
					return completion.commandId == stopCommand;
				});
		},
		std::chrono::seconds(2)));
	const auto stopCompletion = std::ranges::find_if(
		completions, [&](const CommandCompletion &completion) {
			return completion.commandId == stopCommand;
		});
	REQUIRE(stopCompletion != completions.end());
	CHECK(stopCompletion->succeeded);
}

TEST_CASE(
	"GStreamer handles commands and source replacement for generated media")
{
	gst_init(nullptr, nullptr);
	TemporaryMediaFile mediaFile;
	generateTestMedia(mediaFile.path(), 160, 90);

	TestDispatcher dispatcher;
	bool playing = false;
	bool receivedFrame = false;
	bool callbacksOnOwnerThread = true;
	const auto ownerThread = std::this_thread::get_id();
	std::vector<CommandType> completedCommands;
	std::optional<CommandCompletion> failedCommand;
	GStreamerVideoPlayer player(dispatcher.dispatcher());
	player.state.valueChanged()
		.connect([&](const PlaybackState &state) {
			callbacksOnOwnerThread &= std::this_thread::get_id() == ownerThread;
			playing = state == PlaybackState::Playing;
		})
		.release();
	player.frameReady
		.connect([&](std::shared_ptr<const VideoFrame> frame) {
			callbacksOnOwnerThread &= std::this_thread::get_id() == ownerThread;
			receivedFrame = frame && frame->width == 160 &&
							frame->height == 90 &&
							frame->rgba.size() == 160 * 90 * 4;
		})
		.release();
	player.commandCompleted
		.connect([&](const CommandCompletion &completion) {
			callbacksOnOwnerThread &= std::this_thread::get_id() == ownerThread;
			if (completion.succeeded)
				completedCommands.push_back(completion.type);
			else
				failedCommand = completion;
		})
		.release();

	player.open(mediaFile.path().string());
	player.play();
	REQUIRE(dispatcher.waitFor(
		[&] {
			return playing && receivedFrame && completedCommands.size() >= 2;
		},
		std::chrono::seconds(5)));
	CHECK(callbacksOnOwnerThread);
	CHECK(completedCommands[0] == CommandType::Open);
	CHECK(completedCommands[1] == CommandType::Play);
	CHECK(player.duration.get().count() > 0);
	CHECK(player.seekable.get());
	CHECK(player.naturalSize.get() == NaturalSize{160, 90, 1, 1});

	SUBCASE("repeated state and media control commands")
	{
		player.play();
		REQUIRE(
			dispatcher.waitFor([&] { return completedCommands.size() >= 3; },
							   std::chrono::seconds(2)));
		CHECK(completedCommands[2] == CommandType::Play);

		player.pause();
		REQUIRE(dispatcher.waitFor(
			[&] { return !playing && completedCommands.size() >= 4; },
			std::chrono::seconds(2)));
		CHECK(completedCommands[3] == CommandType::Pause);

		player.pause();
		player.setVolume(0.5);
		REQUIRE(
			dispatcher.waitFor([&] { return completedCommands.size() >= 6; },
							   std::chrono::seconds(2)));
		CHECK(completedCommands[4] == CommandType::Pause);
		CHECK(completedCommands[5] == CommandType::SetVolume);
		CHECK(player.volume.get() == doctest::Approx(0.5));
		const auto pausedNotificationCount = dispatcher.notificationCount();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		CHECK(dispatcher.notificationCount() - pausedNotificationCount <= 3);

		player.setMuted(true);
		REQUIRE(
			dispatcher.waitFor([&] { return completedCommands.size() >= 7; },
							   std::chrono::seconds(2)));
		CHECK(completedCommands[6] == CommandType::SetMuted);
		CHECK(player.muted.get());

		player.seek(std::chrono::milliseconds(500));
		REQUIRE(
			dispatcher.waitFor([&] { return completedCommands.size() >= 8; },
							   std::chrono::seconds(2)));
		CHECK(completedCommands[7] == CommandType::Seek);
		CHECK(player.position.get() >= std::chrono::milliseconds(400));
	}

	SUBCASE("invalid replacement clears source state")
	{
		const auto invalidOpen = player.open("/path/that/does/not/exist.ogv");
		REQUIRE(dispatcher.waitFor(
			[&] {
				return failedCommand && failedCommand->commandId == invalidOpen;
			},
			std::chrono::seconds(2)));
		CHECK(player.state.get() == PlaybackState::Empty);
		CHECK(player.position.get() == VideoPlayer::Milliseconds::zero());
		CHECK(player.duration.get() == VideoPlayer::Milliseconds::zero());
		CHECK_FALSE(player.seekable.get());
		CHECK(player.bufferingProgress.get() == 100);
		CHECK(player.naturalSize.get() == NaturalSize{});
		CHECK(player.error.get() == "Source is not a local file");
	}

	SUBCASE("frame callback can destroy its player")
	{
		TestDispatcher destructionDispatcher;
		auto framePlayer = std::make_unique<GStreamerVideoPlayer>(
			destructionDispatcher.dispatcher());
		bool destroyedFromFrame = false;
		framePlayer->frameReady
			.connect([&](std::shared_ptr<const VideoFrame>) {
				framePlayer.reset();
				destroyedFromFrame = true;
			})
			.release();
		framePlayer->open(mediaFile.path().string());
		framePlayer->play();
		REQUIRE(destructionDispatcher.waitFor(
			[&] { return destroyedFromFrame; }, std::chrono::seconds(5)));
		CHECK(framePlayer == nullptr);
	}
}

TEST_CASE("source replacement rejects frames from the previous generation")
{
	gst_init(nullptr, nullptr);
	TemporaryMediaFile firstFile;
	TemporaryMediaFile secondFile;
	generateTestMedia(firstFile.path(), 160, 90);
	generateTestMedia(secondFile.path(), 96, 54);

	struct FrameRecord
	{
		std::uint64_t generation;
		std::uint32_t width;
		std::uint32_t height;
	};
	TestDispatcher dispatcher;
	std::vector<FrameRecord> frames;
	std::optional<CommandCompletion> replacementCompletion;
	std::uint64_t replacementCommandId = 0;
	GStreamerVideoPlayer player(dispatcher.dispatcher());
	player.frameReady
		.connect([&](std::shared_ptr<const VideoFrame> frame) {
			frames.push_back({frame->generation, frame->width, frame->height});
		})
		.release();
	player.commandCompleted
		.connect([&](const CommandCompletion &completion) {
			if (completion.commandId == replacementCommandId)
				replacementCompletion = completion;
		})
		.release();

	player.open(firstFile.path().string());
	player.play();
	REQUIRE(dispatcher.waitFor(
		[&] {
			return std::ranges::any_of(frames, [](const FrameRecord &frame) {
				return frame.width == 160 && frame.height == 90;
			});
		},
		std::chrono::seconds(5)));

	const auto notificationCountBeforeBackpressure =
		dispatcher.notificationCount();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(dispatcher.notificationCount() >
			notificationCountBeforeBackpressure);
	replacementCommandId = player.open(secondFile.path().string());
	player.play();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	frames.clear();
	REQUIRE(dispatcher.waitFor(
		[&] {
			return replacementCompletion &&
				   std::ranges::any_of(frames, [](const FrameRecord &frame) {
					   return frame.width == 96 && frame.height == 54;
				   });
		},
		std::chrono::seconds(5)));
	REQUIRE(replacementCompletion->succeeded);
	REQUIRE_FALSE(frames.empty());
	CHECK(frames.front().width == 96);
	CHECK(frames.front().height == 54);
	for (const auto &frame : frames) {
		if (frame.generation == replacementCompletion->generation) {
			CHECK(frame.width == 96);
			CHECK(frame.height == 54);
		}
	}
}
