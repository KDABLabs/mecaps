#include "http_transfer_handle.h"

HttpTransferHandle::HttpTransferHandle(const Url &url, bool verbose)
	: AbstractTransferHandle(url, verbose)
{
	// switch off progress meter for HTTP requests
	curl_easy_setopt(m_handle, CURLOPT_NOPROGRESS, 1L);
}

std::string HttpTransferHandle::dataRead() const
{
	return m_writeBuffer.str();
}

size_t HttpTransferHandle::writeCallbackImpl(const char *data, size_t size, size_t nmemb)
{
	const size_t realsize = size * nmemb;
	const auto chunk = std::string(data, nmemb);
	m_writeBuffer << chunk;
	return realsize;
}
