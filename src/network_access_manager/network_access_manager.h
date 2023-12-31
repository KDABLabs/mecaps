#pragma once

#include <KDFoundation/file_descriptor_notifier.h>
#include <KDFoundation/timer.h>
#include <map>
#include "abstract_transfer_handle.h"

using namespace KDFoundation;

class NetworkAccessManagerUnitTestHarness;
using FileDescriptorNotifierMap = std::unordered_map<int,std::unique_ptr<FileDescriptorNotifier>>;

class INetworkAccessManager
{
  public:
	virtual bool registerTransfer(AbstractTransferHandle &transferHandle) const = 0;
	virtual bool unregisterTransfer(AbstractTransferHandle &transferHandle) const = 0;

  protected:
	virtual ~INetworkAccessManager() {};
};

class NetworkAccessManager : public INetworkAccessManager
{
	friend class NetworkAccessManagerUnitTestHarness;

  private:
	NetworkAccessManager();
	~NetworkAccessManager();

	NetworkAccessManager(const NetworkAccessManager&) = delete;
	NetworkAccessManager &operator=(const NetworkAccessManager&) = delete;

  public:
	static NetworkAccessManager &instance();

	bool registerTransfer(AbstractTransferHandle &transferHandle) const final;
	bool unregisterTransfer(AbstractTransferHandle &transferHandle) const final;

  private:
	static int socketCallback(CURL *handle, curl_socket_t socket, int eventType, NetworkAccessManager *self, void *);
	static int timerCallback(CURLM *handle, long timeoutMs, Timer *timeoutTimer);

	void onFileDescriptorNotifierTriggered(int nfd, FileDescriptorNotifier::NotificationType fdnType);
	void onTimeoutTimerTriggered();

	void processTransferMessages();
	bool checkCurlMultiResultAndDoDebugPrints(CURLMcode c) const;

	int m_numberOfRunningTransfers;
	CURLM *m_handle;
	Timer m_timeoutTimer;

	struct FileDescriptorNotifierRegistry
	{
		void manageFileDescriptorNotifiers(curl_socket_t socket, int eventType);
		void registerFileDescriptorNotifier(int nfd, FileDescriptorNotifierMap &fdnMap, FileDescriptorNotifier::NotificationType fdnType);
		void unregisterFileDescriptorNotifier(int nfd, FileDescriptorNotifierMap &fdnMap, FileDescriptorNotifier::NotificationType fdnType);

		FileDescriptorNotifierMap readMap;
		FileDescriptorNotifierMap writeMap;
	};
	FileDescriptorNotifierRegistry m_fdnRegistry;

	// DEBUG RELATED LUTs
	static const std::map<int,const std::string> s_curlPollEventToString;
	static const std::map<FileDescriptorNotifier::NotificationType, const std::string> s_notificationTypeToString;
};
