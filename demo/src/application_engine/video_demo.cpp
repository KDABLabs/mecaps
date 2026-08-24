#include "video_demo.h"

#include "gstreamer_video_player.h"

#include <KDUtils/dir.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

int uiMilliseconds(mecaps::video::VideoPlayer::Milliseconds value)
{
	return static_cast<int>(
		std::clamp(value.count(), std::int64_t{0},
				   static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

} // namespace

VideoDemo::VideoDemo(const VideoSingleton &videoSingleton,
					 const slint::ComponentHandle<AppWindow> &appWindow)
	: m_player(std::make_unique<mecaps::video::GStreamerVideoPlayer>(
		  [](std::function<void()> notification) {
			  slint::invoke_from_event_loop(std::move(notification));
		  }))
{
	using namespace mecaps::video;

	auto *player = m_player.get();
	const slint::ComponentWeakHandle<AppWindow> weakWindow(appWindow);
	const auto videoPath =
		KDUtils::Dir::applicationDir().absoluteFilePath("mecaps_demo.webm");
	videoSingleton.set_source_path(slint::SharedString(videoPath));

	videoSingleton.on_open([player, weakWindow] {
		if (auto window = weakWindow.lock())
			player->open(
				(*window)->global<VideoSingleton>().get_source_path().data());
	});
	videoSingleton.on_play([player] { player->play(); });
	videoSingleton.on_pause([player] { player->pause(); });
	videoSingleton.on_stop([player] { player->stop(); });
	videoSingleton.on_seek([player](int milliseconds) {
		player->seek(VideoPlayer::Milliseconds(milliseconds));
	});
	videoSingleton.on_set_volume(
		[player](float volume) { player->setVolume(volume); });
	videoSingleton.on_set_muted(
		[player](bool muted) { player->setMuted(muted); });

	player->state.valueChanged()
		.connect([weakWindow](const PlaybackState &state) {
			const char *text = "Stopped";
			switch (state) {
			case PlaybackState::Empty:
				text = "No media";
				break;
			case PlaybackState::Opening:
				text = "Opening";
				break;
			case PlaybackState::Stopped:
				text = "Stopped";
				break;
			case PlaybackState::Paused:
				text = "Paused";
				break;
			case PlaybackState::Playing:
				text = "Playing";
				break;
			}
			const bool playing = state == PlaybackState::Playing;
			if (auto window = weakWindow.lock()) {
				(*window)->global<VideoSingleton>().set_status(
					slint::SharedString(text));
				(*window)->global<VideoSingleton>().set_playing(playing);
			}
		})
		.release();
	player->position.valueChanged()
		.connect([weakWindow](const VideoPlayer::Milliseconds &value) {
			if (auto window = weakWindow.lock())
				(*window)->global<VideoSingleton>().set_position_ms(
					static_cast<float>(uiMilliseconds(value)));
		})
		.release();
	player->duration.valueChanged()
		.connect([weakWindow](const VideoPlayer::Milliseconds &value) {
			if (auto window = weakWindow.lock())
				(*window)->global<VideoSingleton>().set_duration_ms(
					uiMilliseconds(value));
		})
		.release();
	player->seekable.valueChanged()
		.connect([weakWindow](const bool &value) {
			if (auto window = weakWindow.lock())
				(*window)->global<VideoSingleton>().set_seekable(value);
		})
		.release();
	player->error.valueChanged()
		.connect([weakWindow](const std::string &value) {
			if (auto window = weakWindow.lock())
				(*window)->global<VideoSingleton>().set_error(
					slint::SharedString(value));
		})
		.release();
	player->frameReady
		.connect([weakWindow](std::shared_ptr<const VideoFrame> frame) {
			if (auto window = weakWindow.lock()) {
				slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(
					frame->width, frame->height,
					reinterpret_cast<const slint::Rgba8Pixel *>(
						frame->rgba.data()));
				slint::Image image(std::move(pixels));
				(*window)->global<VideoSingleton>().set_frame(image);
			}
		})
		.release();
}

VideoDemo::~VideoDemo() = default;
