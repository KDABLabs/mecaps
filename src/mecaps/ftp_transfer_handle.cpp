#include "ftp_transfer_handle.h"
#include <cmath>
#include <spdlog/spdlog.h>

AbstractFtpTransferHandle::AbstractFtpTransferHandle(const std::string &url, bool verbose)
	: AbstractTransferHandle(url, verbose)
	, m_file{nullptr}
{
	// switch on progress meter for FTP requests
	curl_easy_setopt(m_handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(m_handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
	curl_easy_setopt(m_handle, CURLOPT_XFERINFODATA, this);
}

int AbstractFtpTransferHandle::progressCallback(AbstractFtpTransferHandle *self, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	return self->progressCallbackImpl(dltotal, dlnow, ultotal, ulnow);
}

int AbstractFtpTransferHandle::calculateProgressPercent(curl_off_t numberOfBytesTransferred, curl_off_t totalNumberOfBytesToTransfer)
{
	return std::round(totalNumberOfBytesToTransfer ? ((100 * numberOfBytesTransferred)/totalNumberOfBytesToTransfer) : 0);
}

FtpDownloadTransferHandle::FtpDownloadTransferHandle(File &file, const std::string &url, bool verbose)
	: AbstractFtpTransferHandle(url, verbose)
{
	m_file = fopen(file.path().c_str(), "wb");
	if (!m_file) {
		spdlog::warn("FtpDownloadTransferHandle::setFile() - cannot create FILE object from file.path()");
		return;
	}

	// overwrite some curl options set in AbstractTransferHandle so that we do not use writeCallback but fwrite to file
	curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, m_file);
}

int FtpDownloadTransferHandle::progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	numberOfBytesTransferred.set(dlnow);
	totalNumberOfBytesToTransfer.set(dltotal);

	return 0;
}

void FtpDownloadTransferHandle::transferDoneCallbackImpl(CURLcode result)
{
	if (m_file) {
		curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, NULL);
		fclose(m_file);
		m_file = nullptr;
	}
}

FtpUploadTransferHandle::FtpUploadTransferHandle(File &file, const std::string &url, bool verbose)
	: AbstractFtpTransferHandle(url, verbose)
{
	m_file = fopen(file.path().c_str(), "rb");
	if (!m_file) {
		spdlog::warn("FtpUploadTransferHandle::setFile() - cannot create FILE object from file.path()");
		return;
	}

	// enable upload
	curl_easy_setopt(m_handle, CURLOPT_UPLOAD, 1L);

	// overwrite some curl options set in AbstractTransferHandle so that we do not use readCallback but fread from file
	curl_easy_setopt(m_handle, CURLOPT_READFUNCTION, NULL);
	curl_easy_setopt(m_handle, CURLOPT_READDATA, m_file);

	const auto fileSize = (curl_off_t)file.size();
	if (fileSize == 0) {
		spdlog::warn("FtpUploadTransferHandle::setFile() - size of specified file is zero");
	}
	else {
		spdlog::info("FtpUploadTransferHandle::setFile() - size of specified file is {} bytes", fileSize);
	}

	// set file size (ultotal) to allow for calculation of progress value in percent
	curl_easy_setopt(m_handle, CURLOPT_INFILESIZE_LARGE, fileSize);
}

int FtpUploadTransferHandle::progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	numberOfBytesTransferred.set(ulnow);
	totalNumberOfBytesToTransfer.set(ultotal);

	return 0;
}

void FtpUploadTransferHandle::transferDoneCallbackImpl(CURLcode result)
{
	if (m_file) {
		curl_easy_setopt(m_handle, CURLOPT_READDATA, NULL);
		fclose(m_file);
		m_file = nullptr;
	}
}
