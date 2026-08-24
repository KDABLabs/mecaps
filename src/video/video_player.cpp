#include "video_player.h"

namespace mecaps::video {

VideoPlayer::VideoPlayer()
	: m_observables(std::make_shared<ObservableState>()),
	  state(m_observables->state), position(m_observables->position),
	  duration(m_observables->duration), seekable(m_observables->seekable),
	  bufferingProgress(m_observables->bufferingProgress),
	  volume(m_observables->volume), muted(m_observables->muted),
	  naturalSize(m_observables->naturalSize), error(m_observables->error),
	  commandCompleted(m_observables->commandCompleted),
	  frameReady(m_observables->frameReady)
{
}

std::shared_ptr<void> VideoPlayer::retainObservables() const
{
	return m_observables;
}

} // namespace mecaps::video
