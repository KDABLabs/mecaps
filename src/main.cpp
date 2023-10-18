#include "mecaps/application_engine.h"
#include "mecaps/mecaps.h"

int main()
{
	const slint::PhysicalSize windowSize(
		slint::Size<uint32_t>{.width = 800, .height = 800});

	// step 1: register GuiApplication singleton
	KDGui::GuiApplication app;
	// this is the supported way to set the window title initially.
	// the title can be changed from `window->title` at any time within the
	// application logic.
	app.applicationName = "Slint/KDGui Integration";

	// step 2: create a window with kdgui and register it with slint
	auto *window = mecaps::createAndIntegrateWindow(windowSize);

	// step 3: create a slint app
	// this *must* happen last, otherwise it will register a default platform
	// and make createAndIntegrateWindow fail with an AlreadySet error
	auto slintApp = AppWindow::create();

	ApplicationEngine::init(slintApp);

	slintApp->run();

	return 0;
}
