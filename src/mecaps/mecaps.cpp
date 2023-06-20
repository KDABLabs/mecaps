#include "mecaps.h"
#include "platform.h"

namespace mecaps {

KDGui::Window *createAndIntegrateWindow(slint::PhysicalSize windowSize)
{
	/// Create a custom platform which serves to wrap the WindowAdapter and
	/// return it when slint queries it internally. Register said platform with
	/// slint.
	auto platform = std::make_unique<mecaps::KDSlintPlatform>();
	auto windowAdapter = std::make_unique<mecaps::KDWindowAdapter>();
	auto *window = &windowAdapter->kdGuiWindow();

	window->width = windowSize.width;
	window->height = windowSize.height;

	platform->aquireWindowAdapter(std::move(windowAdapter));
	slint_platform::Platform::register_platform(std::move(platform));

	return window;
}

} // namespace mecaps
