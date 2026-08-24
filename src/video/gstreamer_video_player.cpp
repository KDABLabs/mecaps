#include "gstreamer_video_player.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace mecaps::video {
namespace {

GStreamerVideoPlayer::Dispatcher
requireDispatcher(GStreamerVideoPlayer::Dispatcher dispatcher)
{
	if (!dispatcher)
		throw std::invalid_argument(
			"GStreamerVideoPlayer requires a dispatcher");
	return dispatcher;
}

struct GstObjectDeleter
{
	template <typename T> void operator()(T *object) const
	{
		if (object)
			gst_object_unref(object);
	}
};

template <typename T> using GstObjectPtr = std::unique_ptr<T, GstObjectDeleter>;

struct PipelineState
{
	GstElement *pipeline = nullptr;
	GstElement *sink = nullptr;
	GstBus *bus = nullptr;
	FrameMailbox *mailbox = nullptr;
	std::function<void(NaturalSize)> naturalSizeChanged;
	NaturalSize naturalSize;
	std::atomic<std::uint64_t> generation = 0;
};

GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData)
{
	auto &state = *static_cast<PipelineState *>(userData);
	GstSample *sample = gst_app_sink_pull_sample(sink);
	if (!sample)
		return GST_FLOW_EOS;

	GstVideoInfo info;
	GstVideoFrame mappedFrame;
	const bool infoValid =
		gst_video_info_from_caps(&info, gst_sample_get_caps(sample));
	const bool mapped =
		infoValid &&
		gst_video_frame_map(&mappedFrame, &info, gst_sample_get_buffer(sample),
							GST_MAP_READ);
	if (!mapped) {
		gst_sample_unref(sample);
		return infoValid ? GST_FLOW_ERROR : GST_FLOW_NOT_NEGOTIATED;
	}
	if (mapped) {
		auto frame = std::make_shared<VideoFrame>();
		frame->generation = state.generation.load();
		frame->width = GST_VIDEO_FRAME_WIDTH(&mappedFrame);
		frame->height = GST_VIDEO_FRAME_HEIGHT(&mappedFrame);
		const NaturalSize size{frame->width, frame->height,
							   GST_VIDEO_INFO_PAR_N(&info),
							   GST_VIDEO_INFO_PAR_D(&info)};
		if (state.naturalSize != size) {
			state.naturalSize = size;
			state.naturalSizeChanged(size);
		}
		const auto rowBytes = static_cast<std::size_t>(frame->width) * 4;
		const auto frameBytes = rowBytes * frame->height;
		frame->rgba.resizeForOverwrite(frameBytes);
		const auto *source = static_cast<const std::uint8_t *>(
			GST_VIDEO_FRAME_PLANE_DATA(&mappedFrame, 0));
		const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&mappedFrame, 0);
		if (stride >= 0 && static_cast<std::size_t>(stride) == rowBytes) {
			std::memcpy(frame->rgba.data(), source, frameBytes);
		} else {
			for (std::uint32_t row = 0; row < frame->height; ++row) {
				const auto sourceOffset =
					static_cast<std::ptrdiff_t>(row) * stride;
				std::memcpy(frame->rgba.data() + row * rowBytes,
							source + sourceOffset, rowBytes);
			}
		}
		state.mailbox->publish(std::move(frame));
		gst_video_frame_unmap(&mappedFrame);
	}
	gst_sample_unref(sample);
	return GST_FLOW_OK;
}

} // namespace

GStreamerVideoPlayer::GStreamerVideoPlayer(Dispatcher dispatcher)
	: GStreamerVideoPlayer(std::move(dispatcher), FactoryNames{})
{
}

GStreamerVideoPlayer::GStreamerVideoPlayer(Dispatcher dispatcher,
										   FactoryNames factoryNames,
										   Milliseconds commandTimeout)
	: m_dispatcher(requireDispatcher(std::move(dispatcher))),
	  m_factoryNames(std::move(factoryNames)),
	  m_commandTimeout(std::max(commandTimeout, Milliseconds::zero())),
	  m_worker([this](std::stop_token stopToken) { run(stopToken); })
{
}

GStreamerVideoPlayer::~GStreamerVideoPlayer()
{
	m_worker.request_stop();
	m_wake.notify_all();
}

void GStreamerVideoPlayer::dispatch(std::function<void()> notification)
{
	const std::weak_ptr<void> lifetime = m_lifetime;
	const auto observableLifetime = retainObservables();
	// The capture keeps signals and properties alive if a callback destroys the
	// player.
	m_dispatcher([lifetime, observableLifetime,
				  notification = std::move(notification)]() mutable {
		if (lifetime.lock()) {
			notification();
		}
	});
}

void GStreamerVideoPlayer::complete(std::uint64_t commandId,
									std::uint64_t generation, CommandType type,
									bool succeeded, std::string errorMessage)
{
	auto *signal = &commandCompleted;
	dispatch(
		[completion = CommandCompletion{commandId, generation, type, succeeded,
										std::move(errorMessage)},
		 signal] { signal->emit(completion); });
}

void GStreamerVideoPlayer::publishFrame(std::shared_ptr<const VideoFrame> frame)
{
	const auto deliveryState = m_frameDeliveryState;
	{
		std::scoped_lock lock(deliveryState->mutex);
		deliveryState->latestFrame = std::move(frame);
		if (deliveryState->deliveryScheduled)
			return;
		deliveryState->deliveryScheduled = true;
	}
	auto *signal = &frameReady;
	dispatch([deliveryState, signal]() mutable {
		std::shared_ptr<const VideoFrame> latestFrame;
		{
			std::scoped_lock lock(deliveryState->mutex);
			latestFrame = std::move(deliveryState->latestFrame);
			deliveryState->deliveryScheduled = false;
		}
		if (latestFrame)
			signal->emit(std::move(latestFrame));
	});
}

std::uint64_t GStreamerVideoPlayer::enqueue(Command command,
											bool advancesGeneration)
{
	std::uint64_t id = 0;
	{
		std::scoped_lock lock(m_mutex);
		command.id = m_nextCommandId++;
		if (advancesGeneration) {
			command.generation = ++m_generation;
			m_mailbox.invalidate(m_generation);
			const auto deliveryState = m_frameDeliveryState;
			std::scoped_lock deliveryLock(deliveryState->mutex);
			deliveryState->latestFrame.reset();
		} else {
			command.generation = m_generation;
		}
		id = command.id;
		m_commands.push_back(std::move(command));
	}
	m_wake.notify_one();
	return id;
}

std::uint64_t GStreamerVideoPlayer::open(std::string localFile)
{
	Command command{};
	command.type = CommandType::Open;
	command.path = std::move(localFile);
	return enqueue(std::move(command), true);
}

std::uint64_t GStreamerVideoPlayer::play()
{
	Command command{};
	command.type = CommandType::Play;
	return enqueue(std::move(command));
}
std::uint64_t GStreamerVideoPlayer::pause()
{
	Command command{};
	command.type = CommandType::Pause;
	return enqueue(std::move(command));
}
std::uint64_t GStreamerVideoPlayer::stop()
{
	Command command{};
	command.type = CommandType::Stop;
	return enqueue(std::move(command), true);
}

std::uint64_t GStreamerVideoPlayer::seek(Milliseconds requestedPosition)
{
	Command command{};
	command.type = CommandType::Seek;
	command.position = std::max(requestedPosition, Milliseconds::zero());
	return enqueue(std::move(command), true);
}

std::uint64_t GStreamerVideoPlayer::setVolume(double requestedVolume)
{
	Command command{};
	command.type = CommandType::SetVolume;
	command.volume = std::clamp(requestedVolume, 0.0, 1.0);
	return enqueue(std::move(command));
}

std::uint64_t GStreamerVideoPlayer::setMuted(bool requestedMuted)
{
	Command command{};
	command.type = CommandType::SetMuted;
	command.muted = requestedMuted;
	return enqueue(std::move(command));
}

void GStreamerVideoPlayer::run(std::stop_token stopToken)
{
	gst_init(nullptr, nullptr);
	PipelineState pipelineState;
	pipelineState.mailbox = &m_mailbox;
	pipelineState.naturalSizeChanged = [this](NaturalSize size) {
		setProperty(naturalSize, size);
	};
	GstObjectPtr<GstElement> pipeline(gst_element_factory_make(
		m_factoryNames.pipeline.c_str(), "mecaps-player"));
	GstObjectPtr<GstElement> sink(gst_element_factory_make(
		m_factoryNames.sink.c_str(), "mecaps-video-sink"));
	pipelineState.pipeline = pipeline.get();
	pipelineState.sink = sink.get();
	const bool backendAvailable = pipelineState.pipeline && pipelineState.sink;
	const std::string backendError =
		"GStreamer playbin or appsink plugin is unavailable";
	GstObjectPtr<GstBus> bus;
	if (backendAvailable) {
		GstCaps *caps = gst_caps_new_simple("video/x-raw", "format",
											G_TYPE_STRING, "RGBA", nullptr);
		g_object_set(pipelineState.sink, "caps", caps, "max-buffers", 2, "drop",
					 TRUE, "sync", TRUE, "emit-signals", TRUE, nullptr);
		gst_caps_unref(caps);
		g_signal_connect(pipelineState.sink, "new-sample",
						 G_CALLBACK(onNewSample), &pipelineState);
		g_object_set(pipelineState.pipeline, "video-sink", pipelineState.sink,
					 nullptr);
		bus.reset(gst_element_get_bus(pipelineState.pipeline));
		pipelineState.bus = bus.get();
	} else {
		setProperty(error, backendError);
	}
	std::optional<Command> pendingStateCommand;
	std::optional<Command> pendingSeekCommand;
	std::optional<std::chrono::steady_clock::time_point> pendingStateDeadline;
	std::optional<std::chrono::steady_clock::time_point> pendingSeekDeadline;
	Milliseconds reportedPosition = Milliseconds::zero();
	Milliseconds reportedDuration = Milliseconds::zero();
	bool reportedSeekable = false;
	const auto reportPosition = [this, &reportedPosition](Milliseconds value) {
		if (reportedPosition != value) {
			reportedPosition = value;
			setProperty(position, value);
		}
	};
	const auto reportDuration = [this, &reportedDuration](Milliseconds value) {
		if (reportedDuration != value) {
			reportedDuration = value;
			setProperty(duration, value);
		}
	};
	const auto reportSeekable = [this, &reportedSeekable](bool value) {
		if (reportedSeekable != value) {
			reportedSeekable = value;
			setProperty(seekable, value);
		}
	};

	while (!stopToken.stop_requested()) {
		Command command{};
		bool hasCommand = false;
		std::vector<std::pair<Command, std::string>> supersededCommands;
		{
			std::unique_lock lock(m_mutex);
			if (backendAvailable) {
				m_wake.wait_for(
					lock, stopToken, std::chrono::milliseconds(10),
					[this, &pendingStateCommand, &pendingSeekCommand] {
						if (!pendingStateCommand && !pendingSeekCommand)
							return !m_commands.empty();
						return std::ranges::any_of(
							m_commands, [](const Command &queuedCommand) {
								return queuedCommand.type ==
										   CommandType::Open ||
									   queuedCommand.type == CommandType::Stop;
							});
					});
			} else {
				m_wake.wait(lock, stopToken,
							[this] { return !m_commands.empty(); });
			}
			const bool commandPending =
				pendingStateCommand || pendingSeekCommand;
			const auto preemptingCommand =
				commandPending
					? std::find_if(
						  m_commands.begin(), m_commands.end(),
						  [](const Command &queuedCommand) {
							  return queuedCommand.type == CommandType::Open ||
									 queuedCommand.type == CommandType::Stop;
						  })
					: m_commands.end();
			if (preemptingCommand != m_commands.end()) {
				const std::string reason =
					preemptingCommand->type == CommandType::Open
						? "Superseded by Open"
						: "Superseded by Stop";
				if (pendingStateCommand) {
					supersededCommands.emplace_back(
						std::move(*pendingStateCommand), reason);
					pendingStateCommand.reset();
					pendingStateDeadline.reset();
				}
				if (pendingSeekCommand) {
					supersededCommands.emplace_back(
						std::move(*pendingSeekCommand), reason);
					pendingSeekCommand.reset();
					pendingSeekDeadline.reset();
				}
				for (auto queuedCommand = m_commands.begin();
					 queuedCommand != preemptingCommand; ++queuedCommand)
					supersededCommands.emplace_back(std::move(*queuedCommand),
													reason);
				command = std::move(*preemptingCommand);
				m_commands.erase(m_commands.begin(),
								 std::next(preemptingCommand));
				hasCommand = true;
			} else if (!m_commands.empty() && !commandPending) {
				command = std::move(m_commands.front());
				m_commands.pop_front();
				hasCommand = true;
				while (command.type == CommandType::Seek &&
					   !m_commands.empty() &&
					   m_commands.front().type == CommandType::Seek) {
					supersededCommands.emplace_back(
						std::move(command), "Superseded by a newer seek");
					command = std::move(m_commands.front());
					m_commands.pop_front();
				}
			}
		}

		for (const auto &[supersededCommand, reason] : supersededCommands)
			complete(supersededCommand.id, supersededCommand.generation,
					 supersededCommand.type, false, reason);

		if (hasCommand && !backendAvailable) {
			pipelineState.generation.store(command.generation);
			setProperty(error, backendError);
			complete(command.id, command.generation, command.type, false,
					 backendError);
			continue;
		}
		if (!backendAvailable)
			continue;

		if (hasCommand) {
			bool succeeded = true;
			bool completionDeferred = false;
			bool completionEmitted = false;
			switch (command.type) {
			case CommandType::Open: {
				gst_element_set_state(pipelineState.pipeline, GST_STATE_NULL);
				gst_bus_set_flushing(pipelineState.bus, TRUE);
				gst_bus_set_flushing(pipelineState.bus, FALSE);
				pipelineState.generation.store(command.generation);
				pipelineState.naturalSize = {};
				setProperty(state, PlaybackState::Empty);
				reportPosition(Milliseconds::zero());
				reportDuration(Milliseconds::zero());
				reportSeekable(false);
				setProperty(bufferingProgress, 100);
				setProperty(naturalSize, NaturalSize{});
				if (!std::filesystem::is_regular_file(command.path)) {
					succeeded = false;
					const std::string message = "Source is not a local file";
					setProperty(error, message);
					complete(command.id, command.generation, command.type,
							 false, message);
					completionEmitted = true;
					break;
				}
				setProperty(error, std::string{});
				std::error_code pathError;
				const auto absolutePath =
					std::filesystem::absolute(command.path, pathError);
				if (pathError) {
					succeeded = false;
					const std::string message =
						"Could not resolve source path: " + pathError.message();
					setProperty(error, message);
					complete(command.id, command.generation, command.type,
							 false, message);
					completionEmitted = true;
					break;
				}
				const auto utf8Path = absolutePath.u8string();
				GError *uriError = nullptr;
				gchar *uri = gst_filename_to_uri(
					reinterpret_cast<const gchar *>(utf8Path.c_str()),
					&uriError);
				if (!uri) {
					succeeded = false;
					std::string message =
						"Could not convert source path to URI";
					if (uriError && uriError->message)
						message += ": " + std::string(uriError->message);
					if (uriError)
						g_error_free(uriError);
					setProperty(error, message);
					complete(command.id, command.generation, command.type,
							 false, message);
					completionEmitted = true;
					break;
				}
				if (uriError)
					g_error_free(uriError);
				g_object_set(pipelineState.pipeline, "uri", uri, nullptr);
				g_free(uri);
				setProperty(state, PlaybackState::Opening);
				const auto stateChange = gst_element_set_state(
					pipelineState.pipeline, GST_STATE_PAUSED);
				succeeded = stateChange != GST_STATE_CHANGE_FAILURE;
				if (stateChange == GST_STATE_CHANGE_ASYNC) {
					pendingStateCommand = command;
					pendingStateDeadline =
						std::chrono::steady_clock::now() + m_commandTimeout;
					completionDeferred = true;
				} else if (succeeded)
					setProperty(state, PlaybackState::Paused);
				break;
			}
			case CommandType::Play: {
				const auto stateChange = gst_element_set_state(
					pipelineState.pipeline, GST_STATE_PLAYING);
				succeeded = stateChange != GST_STATE_CHANGE_FAILURE;
				if (stateChange == GST_STATE_CHANGE_ASYNC) {
					pendingStateCommand = command;
					pendingStateDeadline =
						std::chrono::steady_clock::now() + m_commandTimeout;
					completionDeferred = true;
				} else if (succeeded)
					setProperty(state, PlaybackState::Playing);
				break;
			}
			case CommandType::Pause: {
				const auto stateChange = gst_element_set_state(
					pipelineState.pipeline, GST_STATE_PAUSED);
				succeeded = stateChange != GST_STATE_CHANGE_FAILURE;
				if (stateChange == GST_STATE_CHANGE_ASYNC) {
					pendingStateCommand = command;
					pendingStateDeadline =
						std::chrono::steady_clock::now() + m_commandTimeout;
					completionDeferred = true;
				} else if (succeeded)
					setProperty(state, PlaybackState::Paused);
				break;
			}
			case CommandType::Stop:
				succeeded = gst_element_set_state(pipelineState.pipeline,
												  GST_STATE_NULL) !=
							GST_STATE_CHANGE_FAILURE;
				gst_bus_set_flushing(pipelineState.bus, TRUE);
				gst_bus_set_flushing(pipelineState.bus, FALSE);
				pipelineState.generation.store(command.generation);
				setProperty(state, PlaybackState::Stopped);
				reportPosition(Milliseconds::zero());
				break;
			case CommandType::Seek:
				succeeded = gst_element_seek_simple(
					pipelineState.pipeline, GST_FORMAT_TIME,
					static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
											  GST_SEEK_FLAG_ACCURATE),
					command.position.count() * GST_MSECOND);
				pipelineState.generation.store(command.generation);
				if (succeeded) {
					reportPosition(command.position);
					pendingSeekCommand = command;
					pendingSeekDeadline =
						std::chrono::steady_clock::now() + m_commandTimeout;
					completionDeferred = true;
				}
				break;
			case CommandType::SetVolume:
				g_object_set(pipelineState.pipeline, "volume", command.volume,
							 nullptr);
				setProperty(volume, command.volume);
				break;
			case CommandType::SetMuted:
				g_object_set(pipelineState.pipeline, "mute", command.muted,
							 nullptr);
				setProperty(muted, command.muted);
				break;
			}
			if (!completionDeferred && !completionEmitted)
				complete(command.id, command.generation, command.type,
						 succeeded,
						 succeeded ? std::string()
								   : "GStreamer rejected the command");
		}

		while (GstMessage *message = gst_bus_pop(pipelineState.bus)) {
			switch (GST_MESSAGE_TYPE(message)) {
			case GST_MESSAGE_ERROR: {
				GError *gstError = nullptr;
				gchar *debug = nullptr;
				gst_message_parse_error(message, &gstError, &debug);
				const std::string messageText =
					gstError ? gstError->message : "Unknown GStreamer error";
				setProperty(error, messageText);
				if (pendingStateCommand) {
					complete(pendingStateCommand->id,
							 pendingStateCommand->generation,
							 pendingStateCommand->type, false, messageText);
					pendingStateCommand.reset();
					pendingStateDeadline.reset();
				}
				if (pendingSeekCommand) {
					complete(pendingSeekCommand->id,
							 pendingSeekCommand->generation,
							 pendingSeekCommand->type, false, messageText);
					pendingSeekCommand.reset();
					pendingSeekDeadline.reset();
				}
				if (gstError)
					g_error_free(gstError);
				g_free(debug);
				setProperty(state, PlaybackState::Stopped);
				break;
			}
			case GST_MESSAGE_EOS:
				setProperty(state, PlaybackState::Stopped);
				break;
			case GST_MESSAGE_STATE_CHANGED:
				if (GST_MESSAGE_SRC(message) ==
					GST_OBJECT(pipelineState.pipeline)) {
					GstState newState;
					gst_message_parse_state_changed(message, nullptr, &newState,
													nullptr);
					if (newState == GST_STATE_PLAYING)
						setProperty(state, PlaybackState::Playing);
					else if (newState == GST_STATE_PAUSED)
						setProperty(state, PlaybackState::Paused);
					if (pendingStateCommand) {
						const bool reachedTarget =
							(pendingStateCommand->type == CommandType::Play &&
							 newState == GST_STATE_PLAYING) ||
							((pendingStateCommand->type == CommandType::Open ||
							  pendingStateCommand->type ==
								  CommandType::Pause) &&
							 newState == GST_STATE_PAUSED);
						if (reachedTarget) {
							complete(pendingStateCommand->id,
									 pendingStateCommand->generation,
									 pendingStateCommand->type, true);
							pendingStateCommand.reset();
							pendingStateDeadline.reset();
						}
					}
				}
				break;
			case GST_MESSAGE_ASYNC_DONE:
				if (pendingSeekCommand) {
					complete(pendingSeekCommand->id,
							 pendingSeekCommand->generation,
							 pendingSeekCommand->type, true);
					pendingSeekCommand.reset();
					pendingSeekDeadline.reset();
				}
				break;
			case GST_MESSAGE_BUFFERING: {
				gint percent = 0;
				gst_message_parse_buffering(message, &percent);
				setProperty(bufferingProgress, percent);
				break;
			}
			case GST_MESSAGE_DURATION_CHANGED: {
				gint64 value = 0;
				if (gst_element_query_duration(pipelineState.pipeline,
											   GST_FORMAT_TIME, &value))
					reportDuration(Milliseconds(value / GST_MSECOND));
				break;
			}
			default:
				break;
			}
			gst_message_unref(message);
		}

		const auto now = std::chrono::steady_clock::now();
		if (pendingStateCommand && pendingStateDeadline &&
			now >= *pendingStateDeadline) {
			const std::string message = "GStreamer state command timed out";
			gst_element_set_state(pipelineState.pipeline, GST_STATE_NULL);
			gst_bus_set_flushing(pipelineState.bus, TRUE);
			gst_bus_set_flushing(pipelineState.bus, FALSE);
			setProperty(error, message);
			setProperty(state, PlaybackState::Stopped);
			reportPosition(Milliseconds::zero());
			complete(pendingStateCommand->id, pendingStateCommand->generation,
					 pendingStateCommand->type, false, message);
			pendingStateCommand.reset();
			pendingStateDeadline.reset();
		}
		if (pendingSeekCommand && pendingSeekDeadline &&
			now >= *pendingSeekDeadline) {
			const std::string message = "GStreamer seek command timed out";
			gst_element_set_state(pipelineState.pipeline, GST_STATE_NULL);
			gst_bus_set_flushing(pipelineState.bus, TRUE);
			gst_bus_set_flushing(pipelineState.bus, FALSE);
			setProperty(error, message);
			setProperty(state, PlaybackState::Stopped);
			reportPosition(Milliseconds::zero());
			complete(pendingSeekCommand->id, pendingSeekCommand->generation,
					 pendingSeekCommand->type, false, message);
			pendingSeekCommand.reset();
			pendingSeekDeadline.reset();
		}

		if (!pendingSeekCommand) {
			gint64 value = 0;
			if (gst_element_query_position(pipelineState.pipeline,
										   GST_FORMAT_TIME, &value))
				reportPosition(Milliseconds(value / GST_MSECOND));
		}
		gint64 value = 0;
		if (gst_element_query_duration(pipelineState.pipeline, GST_FORMAT_TIME,
									   &value))
			reportDuration(Milliseconds(value / GST_MSECOND));
		GstQuery *seekingQuery = gst_query_new_seeking(GST_FORMAT_TIME);
		if (gst_element_query(pipelineState.pipeline, seekingQuery)) {
			gboolean canSeek = FALSE;
			gst_query_parse_seeking(seekingQuery, nullptr, &canSeek, nullptr,
									nullptr);
			reportSeekable(canSeek != FALSE);
		}
		gst_query_unref(seekingQuery);
		if (auto frame = m_mailbox.take(pipelineState.generation.load()))
			publishFrame(std::move(frame));
	}

	if (backendAvailable) {
		g_signal_handlers_disconnect_by_data(pipelineState.sink,
											 &pipelineState);
		gst_element_set_state(pipelineState.pipeline, GST_STATE_NULL);
	}
}

} // namespace mecaps::video