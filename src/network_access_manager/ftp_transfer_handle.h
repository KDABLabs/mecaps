#pragma once

#include "abstract_transfer_handle.h"
#include <kdbindings/binding.h>
#include <KDUtils/file.h>

using namespace KDUtils;
using namespace KDBindings;

class AbstractFtpTransferHandle : public AbstractTransferHandle
{
	friend class AbstractFtpTransferHandleUnitTestHarness;

  public:
	explicit AbstractFtpTransferHandle(const Url &url, bool verbose = false);

	Property<curl_off_t> numberOfBytesTransferred { 0 };
	Property<curl_off_t> totalNumberOfBytesToTransfer { 0 };

	Property<int> progressPercent = makeBoundProperty(calculateProgressPercent, numberOfBytesTransferred, totalNumberOfBytesToTransfer);

  protected:
	virtual int progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) = 0;
	FILE *m_file;

  private:
	static int progressCallback(AbstractFtpTransferHandle *self, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
	static int calculateProgressPercent(curl_off_t numberOfBytesTransferred, curl_off_t totalNumberOfBytesToTransfer);
};


class FtpDownloadTransferHandle : public AbstractFtpTransferHandle
{
	friend class FtpDownloadTransferHandleUnitTestHarness;

  public:
	FtpDownloadTransferHandle(File &file, const Url &url, bool verbose = false);

  protected:
	virtual int progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) override;
	virtual void transferDoneCallbackImpl(CURLcode result) override;
};


class FtpUploadTransferHandle : public AbstractFtpTransferHandle
{
	friend class FtpUploadTransferHandleUnitTestHarness;

  public:
	FtpUploadTransferHandle(File &file, const Url &url, bool verbose = false);

  protected:
	virtual int progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) override;
	virtual void transferDoneCallbackImpl(CURLcode result) override;
};
