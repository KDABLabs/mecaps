#pragma once

#include <curl/curl.h>
#include <kdbindings/signal.h>
#include <string>

class AbstractTransferHandle
{
	friend class NetworkAccessManager;

  public:
	explicit AbstractTransferHandle(std::string url, bool verbose = false);
	~AbstractTransferHandle();

	KDBindings::Signal<int> finished;

	static AbstractTransferHandle *fromCurlEasyHandle(CURL *easyHandle);

	CURL *handle() const;
	const std::string &url() const;
	std::string error() const;

  protected:
	virtual size_t readCallbackImpl(char *data, size_t size, size_t nmemb);
	virtual size_t writeCallbackImpl(char *data, size_t size, size_t nmemb);
	virtual void transferDoneCallbackImpl(CURLcode result) {}

	CURL *m_handle;
	std::string m_url;
	char m_errorBuffer[CURL_ERROR_SIZE];

  private:
	static size_t readCallback(char *data, size_t size, size_t nmemb, AbstractTransferHandle *self);
	static size_t writeCallback(char *data, size_t size, size_t nmemb, AbstractTransferHandle *self);
	void transferDoneCallback(CURLcode result);
};
