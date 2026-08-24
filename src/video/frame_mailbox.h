#pragma once

#include "video_player.h"

#include <mutex>

namespace mecaps::video {

class FrameMailbox
{
  public:
	void publish(std::shared_ptr<const VideoFrame> frame);
	std::shared_ptr<const VideoFrame> take(std::uint64_t generation);
	void invalidate(std::uint64_t generation);

  private:
	std::mutex m_mutex;
	std::uint64_t m_generation = 0;
	std::shared_ptr<const VideoFrame> m_frame;
};

} // namespace mecaps::video