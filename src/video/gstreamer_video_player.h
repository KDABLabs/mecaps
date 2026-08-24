#pragma once

#include "frame_mailbox.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace mecaps::video {

class GStreamerVideoPlayer final : public VideoPlayer
{
  public:
	// Must enqueue notifications in FIFO order on the thread that owns the
	// Must accept concurrent submissions, preserve each producer's submission
	// order, and execute every notification on the thread that owns the player.
	using Dispatcher = std::function<void(std::function<void()>)>;
	struct FactoryNames
	{
		std::string pipeline = "playbin";
		std::string sink = "appsink";
	};

	explicit GStreamerVideoPlayer(Dispatcher dispatcher);
	GStreamerVideoPlayer(Dispatcher dispatcher, FactoryNames factoryNames,
						 Milliseconds commandTimeout = std::chrono::seconds(5));
	~GStreamerVideoPlayer() override;

	std::uint64_t open(std::string localFile) override;
	std::uint64_t play() override;
	std::uint64_t pause() override;
	std::uint64_t stop() override;
	std::uint64_t seek(Milliseconds position) override;
	std::uint64_t setVolume(double volume) override;
	std::uint64_t setMuted(bool muted) override;

  private:
	struct FrameDeliveryState
	{
		std::mutex mutex;
		std::shared_ptr<const VideoFrame> latestFrame;
		bool deliveryScheduled = false;
	};

	struct Command
	{
		std::uint64_t id;
		std::uint64_t generation;
		CommandType type;
		std::string path;
		Milliseconds position{};
		double volume = 1.0;
		bool muted = false;
	};

	template <typename T>
	void setProperty(KDBindings::Property<T> &property, T value)
	{
		dispatch([property = &property, value = std::move(value)]() mutable {
			property->set(std::move(value));
		});
	}

	void dispatch(std::function<void()> notification);
	void complete(std::uint64_t commandId, std::uint64_t generation,
				  CommandType type, bool succeeded, std::string error = {});
	void publishFrame(std::shared_ptr<const VideoFrame> frame);
	std::uint64_t enqueue(Command command, bool advancesGeneration = false);
	void run(std::stop_token stopToken);

	Dispatcher m_dispatcher;
	FactoryNames m_factoryNames;
	Milliseconds m_commandTimeout;
	std::shared_ptr<void> m_lifetime = std::make_shared<int>(0);
	std::mutex m_mutex;
	std::condition_variable_any m_wake;
	std::deque<Command> m_commands;
	std::uint64_t m_nextCommandId = 1;
	std::uint64_t m_generation = 0;
	FrameMailbox m_mailbox;
	std::shared_ptr<FrameDeliveryState> m_frameDeliveryState =
		std::make_shared<FrameDeliveryState>();
	std::jthread m_worker;
};

} // namespace mecaps::video