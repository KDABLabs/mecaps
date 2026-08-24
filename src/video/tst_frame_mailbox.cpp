#include "frame_mailbox.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace mecaps::video;

TEST_CASE("frame mailbox retains only the latest frame")
{
	FrameMailbox mailbox;
	mailbox.invalidate(7);
	auto first = std::make_shared<VideoFrame>();
	first->generation = 7;
	first->width = 1;
	auto second = std::make_shared<VideoFrame>();
	second->generation = 7;
	second->width = 2;

	mailbox.publish(first);
	mailbox.publish(second);

	CHECK(mailbox.take(7) == second);
	CHECK(mailbox.take(7) == nullptr);
}

TEST_CASE("frame mailbox rejects stale generations")
{
	FrameMailbox mailbox;
	mailbox.invalidate(2);
	auto stale = std::make_shared<VideoFrame>();
	stale->generation = 1;
	mailbox.publish(stale);

	CHECK(mailbox.take(2) == nullptr);

	auto current = std::make_shared<VideoFrame>();
	current->generation = 2;
	mailbox.publish(current);
	mailbox.invalidate(3);
	CHECK(mailbox.take(2) == nullptr);
	CHECK(mailbox.take(3) == nullptr);
}
