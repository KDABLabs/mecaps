#pragma once

#include "app_window.h"
#include "video_player.h"

#include <memory>

class VideoDemo
{
  public:
	VideoDemo(const VideoSingleton &videoSingleton,
			  const slint::ComponentHandle<AppWindow> &appWindow);
	~VideoDemo();

	VideoDemo(const VideoDemo &) = delete;
	VideoDemo &operator=(const VideoDemo &) = delete;

  private:
	std::unique_ptr<mecaps::video::VideoPlayer> m_player;
};
