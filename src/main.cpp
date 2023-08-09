#include "mecaps/http_transfer_handle.h"
#include "mecaps/mecaps.h"
#include "mecaps/network_access_manager.h"
#include "appwindow.h"

int main()
{
	const slint::PhysicalSize windowSize(
		slint::Size<uint32_t>{.width = 800, .height = 600});

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

	// set up everything related to CURL
	NetworkAccessManager &manager = NetworkAccessManager::instance();

	HttpTransferHandle *httpTransfer;
	auto onHttpExampleTransferFinished = [&](int result) {
		spdlog::info("HttpTransferHandle::finished() signal triggered for URL {}", httpTransfer->url());
		if (result == CURLcode::CURLE_OK) {
			spdlog::info("Data:\n{}", httpTransfer->dataRead());
			slintApp->set_fetchedContent(slint::SharedString(httpTransfer->dataRead()));
		}
		else {
			slintApp->set_fetchedContent(slint::SharedString());
		}

		delete httpTransfer;
	};

	// set up application logic
	slintApp->on_request_increase_value(
		[&] {
			slintApp->set_counter(slintApp->get_counter() + 1);
		});

	slintApp->on_query_url(
		[&] {
			const auto url = slintApp->get_url().data();
			httpTransfer = new HttpTransferHandle(url, false);
			httpTransfer->finished.connect(onHttpExampleTransferFinished);
			manager.registerTransfer(httpTransfer);
		});

	slintApp->run();

	return 0;
}
