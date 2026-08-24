#include "frame_mailbox.h"

namespace mecaps::video {

void FrameMailbox::publish(std::shared_ptr<const VideoFrame> frame)
{
	std::scoped_lock lock(m_mutex);
	if (frame && frame->generation == m_generation)
		m_frame = std::move(frame);
}

std::shared_ptr<const VideoFrame> FrameMailbox::take(std::uint64_t generation)
{
	std::scoped_lock lock(m_mutex);
	if (generation != m_generation)
		return {};
	return std::move(m_frame);
}

void FrameMailbox::invalidate(std::uint64_t generation)
{
	std::scoped_lock lock(m_mutex);
	m_generation = generation;
	m_frame.reset();
}

} // namespace mecaps::video