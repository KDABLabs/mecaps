#include "application_engine.h"
#include "ftp_transfer_handle.h"
#include "http_transfer_handle.h"
#include "network_access_manager.h"
#include <spdlog/spdlog.h>

ApplicationEngine &ApplicationEngine::init(const slint::ComponentHandle<AppWindow> &appWindow)
{
	static ApplicationEngine s_instance(appWindow);
	return s_instance;
}

ApplicationEngine::ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow)
{
	InitCounterDemo(appWindow->global<CounterSingleton>());
	InitHttpDemo(appWindow->global<HttpSingleton>());
	InitFtpDemo(appWindow->global<FtpSingleton>());
}

ApplicationEngine::~ApplicationEngine()
{

}

void ApplicationEngine::InitCounterDemo(const CounterSingleton &uiPageCounter)
{
	auto incrementCounterValue = [&]() {
		const auto newCounterValue = uiPageCounter.get_counter() + 1;
		uiPageCounter.set_counter(newCounterValue);
	};
	uiPageCounter.on_request_increase_value(incrementCounterValue);
}

void ApplicationEngine::InitHttpDemo(const HttpSingleton &httpSingleton)
{
	static auto &uiPageHttp = httpSingleton;
	static HttpTransferHandle *httpTransfer = nullptr;

	uiPageHttp.set_url("https://example.com");

	auto onHttpTransferFinished = [&](int result) mutable {
		spdlog::info("HttpTransferHandle::finished() signal triggered for URL {}", httpTransfer->url());
		const auto fetchedContent = (result == CURLcode::CURLE_OK) ? httpTransfer->dataRead() : std::string();
		uiPageHttp.set_fetched_content(slint::SharedString(fetchedContent));
		delete httpTransfer;
	};

	auto startHttpQuery = [this, onHttpTransferFinished]() mutable {
		const std::string &url = uiPageHttp.get_url().data();
		httpTransfer = new HttpTransferHandle(url, true);
		httpTransfer->finished.connect(onHttpTransferFinished);
		NetworkAccessManager::instance().registerTransfer(httpTransfer);
	};
	uiPageHttp.on_request_http_query(startHttpQuery);
}

void ApplicationEngine::InitFtpDemo(const FtpSingleton &ftpSingleton)
{
	static auto &uiPageFtp = ftpSingleton;
	static FtpDownloadTransferHandle *ftpDownloadTransfer = nullptr;
	static FtpUploadTransferHandle *ftpUploadTransfer = nullptr;

	static File ftpFile = File("ls-lR.gz");
	uiPageFtp.set_url_download("ftp://ftp-stud.hs-esslingen.de/debian/ls-lR.gz");
	uiPageFtp.set_url_upload("ftp://ftp.cs.brown.edu/incoming/ls-lR.gz");

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
		NetworkAccessManager::instance().registerTransfer(ftpDownloadTransfer);
		uiPageFtp.set_is_downloading(true);
	};
	uiPageFtp.on_request_ftp_download( [&] { startFtpDownload(); } );

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
		NetworkAccessManager::instance().registerTransfer(ftpUploadTransfer);
		uiPageFtp.set_is_uploading(true);
	};
	uiPageFtp.on_request_ftp_upload( [&] { startFtpUpload(); } );
}
