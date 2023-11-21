#include "abstract_transfer_handle.h"
#include <spdlog/spdlog.h>

// TODO -> allow for reusing handles for multiple transfers
// "re-using handles is a key to good performance with libcurl" -> see https://curl.se/libcurl/c/curl_easy_cleanup.html

AbstractTransferHandle::AbstractTransferHandle(const Url &url, bool verbose)
	: m_url{url}
{
	m_handle = curl_easy_init();
	if (!m_handle) {
		spdlog::error("curl_easy_init()");
		return;
	}
	curl_easy_setopt(m_handle, CURLOPT_URL, url.url().c_str());
	curl_easy_setopt(m_handle, CURLOPT_VERBOSE, verbose ? 1L : 0L);
	curl_easy_setopt(m_handle, CURLOPT_PRIVATE, this); // complementing method making use of this is -> fromCurlEasyHandle()

	curl_easy_setopt(m_handle, CURLOPT_ERRORBUFFER, m_errorBuffer);

	curl_easy_setopt(m_handle, CURLOPT_READFUNCTION, readCallback);
	curl_easy_setopt(m_handle, CURLOPT_READDATA, this);

	curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, this);
}

AbstractTransferHandle::~AbstractTransferHandle()
{
	curl_easy_cleanup(m_handle);
}

AbstractTransferHandle *AbstractTransferHandle::fromCurlEasyHandle(CURL *easyHandle)
{
	AbstractTransferHandle *transferHandle;
	curl_easy_getinfo(easyHandle, CURLINFO_PRIVATE, &transferHandle);
	return transferHandle;
}

CURL *AbstractTransferHandle::handle() const
{
	return m_handle;
}

const Url &AbstractTransferHandle::url() const
{
	return m_url;
}

std::string AbstractTransferHandle::error() const
{
	return std::string(m_errorBuffer);
}

size_t AbstractTransferHandle::readCallbackImpl(char *data, size_t size, size_t nmemb)
{
	const size_t realSize = size * nmemb;
	{
		static bool suppressWarning = false;
		if (!suppressWarning) {
			suppressWarning = true;
			spdlog::warn("Base class method AbstractTransferHandle::readCallbackImpl() was called. Did you miss to implement a method specific to your Transfer type?");
		}
	}
	return realSize;
}

size_t AbstractTransferHandle::writeCallbackImpl(char *data, size_t size, size_t nmemb)
{
	const size_t realSize = size * nmemb;
	{
		static bool suppressWarning = false;
		if (!suppressWarning) {
			suppressWarning = true;
			spdlog::warn("Base class method AbstractTransferHandle::writeCallbackImpl() was called. Did you miss to implement a method specific to your Transfer type?");
		}
	}
	return realSize;
}

size_t AbstractTransferHandle::readCallback(char *data, size_t size, size_t nmemb, AbstractTransferHandle *self)
{
	return self->readCallbackImpl(data, size, nmemb);
}

size_t AbstractTransferHandle::writeCallback(char *data, size_t size, size_t nmemb, AbstractTransferHandle *self)
{
	return self->writeCallbackImpl(data, size, nmemb);
}

void AbstractTransferHandle::transferDoneCallback(CURLcode result)
{
	transferDoneCallbackImpl(result);

	if (result != CURLcode::CURLE_OK) {
		spdlog::error("curl transfer finished with code {} ({}) {}", static_cast<int>(result), curl_easy_strerror(result), error());
	}

	finished.emit((int)result);
}
