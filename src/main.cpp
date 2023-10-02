#include "mecaps/ftp_transfer_handle.h"
#include "mecaps/http_transfer_handle.h"
#include "mecaps/mecaps.h"
#include "mecaps/network_access_manager.h"
#include "app_window.h"

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
	auto &uiPageCounter = slintApp->global<CounterSingleton>();
	auto &uiPageFtp = slintApp->global<FtpSingleton>();
	auto &uiPageHttp = slintApp->global<HttpSingleton>();

	// set up everything related to CURL
	NetworkAccessManager &manager = NetworkAccessManager::instance();

	HttpTransferHandle *httpTransfer;
	auto onHttpExampleTransferFinished = [&](int result) {
		spdlog::info("HttpTransferHandle::finished() signal triggered for URL {}", httpTransfer->url());
		const auto fetchedContent = (result == CURLcode::CURLE_OK) ? httpTransfer->dataRead() : std::string();
		uiPageHttp.set_fetched_content(slint::SharedString(fetchedContent));
		delete httpTransfer;
	};
	auto startHttpQuery = [&]() {
		const std::string &url = uiPageHttp.get_url().data();
		httpTransfer = new HttpTransferHandle(url, false);
		httpTransfer->finished.connect(onHttpExampleTransferFinished);
		manager.registerTransfer(httpTransfer);
	};

	File ftpFile = File("ls-lR.gz");
	FtpUploadTransferHandle *ftpUploadTransfer = nullptr;
	FtpDownloadTransferHandle *ftpDownloadTransfer = nullptr;

	auto onFtpExampleUploadTransferFinished = [&]() {
		spdlog::info("FtpUploadTransferHandle::finished() - uploaded {} bytes", ftpUploadTransfer->numberOfBytesTransferred.get());
		uiPageFtp.set_is_uploading(false);
		delete ftpUploadTransfer;
	};
	auto onFtpExampleUploadTransferProgressPercentChanged = [&](const int &progressPercent) {
		uiPageFtp.set_progress_percent_upload(progressPercent);
	};
	auto startFtpUpload = [&]() {
		const std::string &url = uiPageFtp.get_url_upload().data();
		ftpUploadTransfer = new FtpUploadTransferHandle(ftpFile, url, true);
		ftpUploadTransfer->finished.connect(onFtpExampleUploadTransferFinished);
		ftpUploadTransfer->progressPercent.valueChanged().connect(onFtpExampleUploadTransferProgressPercentChanged);
		manager.registerTransfer(ftpUploadTransfer);
		uiPageFtp.set_is_uploading(true);
	};

	auto onFtpExampleDownloadTransferFinished = [&]() {
		spdlog::info("FtpDownloadTransferHandle::finished() - downloaded {} bytes", ftpDownloadTransfer->numberOfBytesTransferred.get());
		uiPageFtp.set_is_downloading(false);
		delete ftpDownloadTransfer;
	};
	auto onFtpExampleDownloadTransferProgressPercentChanged = [&](const int &progressPercent) {
		uiPageFtp.set_progress_percent_download(progressPercent);
	};
	auto startFtpDownload = [&]() {
		const std::string &url = uiPageFtp.get_url_download().data();
		ftpDownloadTransfer = new FtpDownloadTransferHandle(ftpFile, url, false);
		ftpDownloadTransfer->finished.connect(onFtpExampleDownloadTransferFinished);
		ftpDownloadTransfer->progressPercent.valueChanged().connect(onFtpExampleDownloadTransferProgressPercentChanged);
		manager.registerTransfer(ftpDownloadTransfer);
		uiPageFtp.set_is_downloading(true);
	};

	// set up curl related UI defaults
	uiPageHttp.set_url("https://example.com");
	uiPageFtp.set_url_download("ftp://ftp-stud.hs-esslingen.de/debian/ls-lR.gz");
	uiPageFtp.set_url_upload("ftp://ftp.cs.brown.edu/incoming/ls-lR.gz");

	// set up application logic
	uiPageCounter.on_request_increase_value( [&] { uiPageCounter.set_counter(uiPageCounter.get_counter() + 1); } );
	uiPageHttp.on_request_http_query( [&] { startHttpQuery(); } );
	uiPageFtp.on_request_ftp_download( [&] { startFtpDownload(); } );
	uiPageFtp.on_request_ftp_upload( [&] { startFtpUpload(); } );

	slintApp->run();

	return 0;
}
