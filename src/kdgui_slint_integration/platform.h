#include "window_adapter.h"
#include <KDGui/gui_application.h>
#include <slint-platform.h>
#include <thread>

namespace mecaps {
namespace slint_platform = slint::platform;
class KDSlintPlatform : public slint_platform::Platform
{
  public:
	inline void aquireWindowAdapter(std::unique_ptr<KDWindowAdapter> &&window)
	{
		m_windowAdapter.swap(window);
	}

	// The logic here would just be
	// "while (!m_needsToQuit) app->processEvents()" except we want to support
	// run_from_event_loop and quit_event_loop in a thread safe way
	inline void run_event_loop() override
	{
		auto *app = KDGui::GuiApplication::instance();

		while (true) {
			std::optional<slint_platform::Platform::Task> event;
			{
				// take ownership of slint event queue and m_needsToQuit
				std::unique_lock lock(the_mutex);

				// if there are no slint events to process, loop on waiting for
				// kdgui events
				if (m_events.empty()) {
					if (m_needsToQuit) {
						m_needsToQuit = false;
						break;
					}

					// don't own the event queue so that kdgui event handlers
					// can submit to it without causing deadlock
					lock.unlock();
					while (true) {
						slint_platform::update_timers_and_animations();

						auto maxAcceptableTimeout =
							slint_platform::duration_until_next_timer_update();
						if (maxAcceptableTimeout) {
							app->processEvents(static_cast<int>(
								maxAcceptableTimeout->count()));
						} else {
							// block slint events, timers, and animations
							// indefinitely
							app->processEvents(-1);
						}

						// check if a slint event has arrived
						// NOTE: we do not own this here, but it's atomic
						if (m_slintStateChanged)
							break;
					}
					continue;
				}

				event = std::move(m_events.front());
				m_events.pop_front();
			}

			if (event) {
				std::move(*event).run();
				event.reset();
			}
		}

		app->quit();
	}

	inline void quit_event_loop() override
	{
		std::unique_lock lock(the_mutex);
		m_needsToQuit = true;
		m_slintStateChanged = true;
	}

	inline void run_in_event_loop(slint_platform::Platform::Task event) override
	{
		const std::unique_lock lock(the_mutex);
		m_events.push_back(std::move(event));
		m_slintStateChanged = true;
		KDFoundation::CoreApplication::instance()->eventLoop()->wakeUp();
	}

  protected:
	inline std::unique_ptr<slint_platform::WindowAdapter>
	create_window_adapter() override
	{
		return std::move(m_windowAdapter);
	}

  private:
	std::mutex the_mutex; // owns m_needsToQuit and m_events
	bool m_needsToQuit = false;
	std::deque<slint_platform::Platform::Task> m_events;

	std::atomic<bool> m_slintStateChanged;

	std::unique_ptr<KDWindowAdapter> m_windowAdapter;
};
} // namespace mecaps
