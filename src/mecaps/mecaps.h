#include "slint_size.h"
#include <KDGui/gui_application.h>
#include <KDGui/window.h>

namespace mecaps {

/// Register an create a window and window adapter and register them, so events
/// for the GuiApplication get propagated to slint.
KDGui::Window *createAndIntegrateWindow(slint::PhysicalSize windowSize);

/// Runs the slint app in the given slint-integrated window.
/// @pre: a KDGui::GuiApplication has been instantiated.
/// @pre: SlintApp must implement void show() and void hide()
template <typename SlintApp>
int run(KDGui::Window *window, SlintApp slintAppHandle)
{
	auto *app = KDGui::GuiApplication::instance();

	bool windowVisible = true;
	window->visible.valueChanged().connect([&](bool visible) {
		windowVisible = visible;
		if (!visible) {
			slintAppHandle->hide();
			app->quit();
		}
	});

	slintAppHandle->show();

	// NOLINTNEXTLINE
	while (windowVisible) {
		app->processEvents();
	}

	return app->exec();
}

} // namespace mecaps
