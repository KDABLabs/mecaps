#pragma once

#include <kdbindings/property.h>
#include <kdbindings/signal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mecaps::video {

enum class PlaybackState
{
	Empty,
	Opening,
	Stopped,
	Paused,
	Playing
};

enum class CommandType
{
	Open,
	Play,
	Pause,
	Stop,
	Seek,
	SetVolume,
	SetMuted
};

struct NaturalSize
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t pixelAspectRatioNumerator = 1;
	std::uint32_t pixelAspectRatioDenominator = 1;

	bool operator==(const NaturalSize &) const = default;
};

class RgbaBuffer
{
  public:
	void resizeForOverwrite(std::size_t size)
	{
		if (size == 0) {
			m_data.reset();
		} else if (size != m_size) {
			m_data = std::make_unique_for_overwrite<std::uint8_t[]>(size);
		}
		m_size = size;
	}

	std::uint8_t *data() { return m_data.get(); }
	const std::uint8_t *data() const { return m_data.get(); }
	std::size_t size() const { return m_size; }
	bool empty() const { return m_size == 0; }

  private:
	std::unique_ptr<std::uint8_t[]> m_data;
	std::size_t m_size = 0;
};

struct VideoFrame
{
	std::uint64_t generation = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	RgbaBuffer rgba;
};

struct CommandCompletion
{
	std::uint64_t commandId = 0;
	std::uint64_t generation = 0;
	CommandType type = CommandType::Open;
	bool succeeded = false;
	std::string error;
};

class VideoPlayer
{
	struct ObservableState
	{
		KDBindings::Property<PlaybackState> state{PlaybackState::Empty};
		KDBindings::Property<std::chrono::milliseconds> position{
			std::chrono::milliseconds::zero()};
		KDBindings::Property<std::chrono::milliseconds> duration{
			std::chrono::milliseconds::zero()};
		KDBindings::Property<bool> seekable{false};
		KDBindings::Property<int> bufferingProgress{100};
		KDBindings::Property<double> volume{1.0};
		KDBindings::Property<bool> muted{false};
		KDBindings::Property<NaturalSize> naturalSize{};
		KDBindings::Property<std::string> error{};
		KDBindings::Signal<const CommandCompletion &> commandCompleted;
		KDBindings::Signal<std::shared_ptr<const VideoFrame>> frameReady;
	};

	std::shared_ptr<ObservableState> m_observables;

  public:
	using Milliseconds = std::chrono::milliseconds;

	// Properties and signals belong to the owner thread selected by the
	// backend. Read properties and manage signal connections only from that
	// thread.
	virtual ~VideoPlayer() = default;

	VideoPlayer(const VideoPlayer &) = delete;
	VideoPlayer &operator=(const VideoPlayer &) = delete;

	virtual std::uint64_t open(std::string localFile) = 0;
	virtual std::uint64_t play() = 0;
	virtual std::uint64_t pause() = 0;
	virtual std::uint64_t stop() = 0;
	virtual std::uint64_t seek(Milliseconds position) = 0;
	virtual std::uint64_t setVolume(double volume) = 0;
	virtual std::uint64_t setMuted(bool muted) = 0;

	KDBindings::Property<PlaybackState> &state;
	KDBindings::Property<Milliseconds> &position;
	KDBindings::Property<Milliseconds> &duration;
	KDBindings::Property<bool> &seekable;
	KDBindings::Property<int> &bufferingProgress;
	KDBindings::Property<double> &volume;
	KDBindings::Property<bool> &muted;
	KDBindings::Property<NaturalSize> &naturalSize;
	KDBindings::Property<std::string> &error;

	KDBindings::Signal<const CommandCompletion &> &commandCompleted;
	KDBindings::Signal<std::shared_ptr<const VideoFrame>> &frameReady;

  protected:
	VideoPlayer();
	std::shared_ptr<void> retainObservables() const;
};

} // namespace mecaps::video