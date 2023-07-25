#include "mecaps.h"
#include "platform.h"

namespace mecaps {

// TODO: once slint implements set_size in their CppWindowAdapter, we'll
// actually handle creating the window with a given size
KDGui::Window *createAndIntegrateWindow(slint::PhysicalSize /*windowSize*/)
{
	auto platform = std::make_unique<mecaps::KDSlintPlatform>();
	auto windowAdapter = std::make_unique<mecaps::KDWindowAdapter>();
	auto *window = &windowAdapter->kdGuiWindow();

	platform->aquireWindowAdapter(std::move(windowAdapter));
	slint_platform::set_platform(std::move(platform));

	window->visible.valueChanged().connect([&](bool visible) {
		if (!visible) {
			slint::quit_event_loop();
		}
	});

	return window;
}

} // namespace mecaps
