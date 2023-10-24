#pragma once

#include "abstract_transfer_handle.h"
#include <sstream>

class HttpTransferHandle : public AbstractTransferHandle
{
  public:
	explicit HttpTransferHandle(const Url &url, bool verbose = false);

	std::string dataRead() const;

  protected:
	virtual size_t writeCallbackImpl(char *data, size_t size, size_t nmemb) override;
	virtual void transferDoneCallbackImpl(CURLcode result) override { /* nothing to do */ }

	std::ostringstream m_writeBuffer; // TODO -> this is most probalby not how we want to maintain data read during transfer(?)

  private:

};
