#include "network_access_manager.h"

#include <functional>
#include <spdlog/spdlog.h>

const std::map<int,const std::string> NetworkAccessManager::s_curlPollEventToString = {
	{CURL_POLL_NONE,"CURL_POLL_NONE"},
	{CURL_POLL_IN,"CURL_POLL_IN"},
	{CURL_POLL_OUT,"CURL_POLL_OUT"},
	{CURL_POLL_INOUT,"CURL_POLL_INOUT"},
	{CURL_POLL_REMOVE,"CURL_POLL_REMOVE"},
	};

const std::map<FileDescriptorNotifier::NotificationType, const std::string> NetworkAccessManager::s_notificationTypeToString = {
	{FileDescriptorNotifier::NotificationType::Exception,"NotificationType::Exception"},
	{FileDescriptorNotifier::NotificationType::Read,"NotificationType::Read"},
	{FileDescriptorNotifier::NotificationType::Write,"NotificationType::Write"},
	};

NetworkAccessManager &NetworkAccessManager::instance()
{
	static NetworkAccessManager s_instance;
	return s_instance;
}

void NetworkAccessManager::registerTransfer(AbstractTransferHandle *transferHandle)
{
	spdlog::debug("NetworkAccessManager::registerTransfer()");
	auto rc = curl_multi_add_handle(m_handle, transferHandle->handle());
	checkCurlMultiResultAndDoDebugPrints(rc);
}

void NetworkAccessManager::unregisterTransfer(AbstractTransferHandle *transferHandle)
{
	spdlog::debug("NetworkAccessManager::unregisterTransfer()");
	auto rc = curl_multi_remove_handle(m_handle, transferHandle->handle());
	checkCurlMultiResultAndDoDebugPrints(rc);
}

NetworkAccessManager::NetworkAccessManager()
{
	curl_global_init(CURL_GLOBAL_ALL);

	m_handle = curl_multi_init();
	if (m_handle == nullptr) {
		spdlog::critical("NetworkAccessManager::NetworkAccessManager() - curl_multi_init() returned nullptr");
		return;
	}

	CURLMcode rc;
	rc = curl_multi_setopt(m_handle, CURLMOPT_SOCKETFUNCTION, socketCallback);
	checkCurlMultiResultAndDoDebugPrints(rc);
	rc = curl_multi_setopt(m_handle, CURLMOPT_SOCKETDATA, this);
	checkCurlMultiResultAndDoDebugPrints(rc);

	rc = curl_multi_setopt(m_handle, CURLMOPT_TIMERFUNCTION, timerCallback);
	checkCurlMultiResultAndDoDebugPrints(rc);
	rc = curl_multi_setopt(m_handle, CURLMOPT_TIMERDATA, &m_timeoutTimer);
	checkCurlMultiResultAndDoDebugPrints(rc);

	m_timeoutTimer.timeout.connect([this]() {
		m_timeoutTimer.running.set(false);
		onTimeoutTimerTriggered();
	});
}

NetworkAccessManager::~NetworkAccessManager()
{
	curl_multi_cleanup(NetworkAccessManager::m_handle);
}

int NetworkAccessManager::socketCallback(CURL *handle, curl_socket_t socket, int eventType, NetworkAccessManager *self, void *)
{
	spdlog::debug("NetworkAccessManager::socketCallback() - socket:{}, event:{}", socket, s_curlPollEventToString.at(eventType));

	self->m_fdnRegistry.manageFileDescriptorNotifiers(socket, eventType);

	return 0;
}

int NetworkAccessManager::timerCallback(CURLM *handle, long timeoutMs, Timer *timeoutTimer)
{
	spdlog::debug("NetworkAccessManager::timerCallback() - timeout:{} ms)", timeoutMs);

	const auto curlRequestsTimeoutTimerToBeStopped = (timeoutMs == -1);
	const auto curlRequestsTimeoutTimerToTimeoutAsap = (timeoutMs == 0);

	if (curlRequestsTimeoutTimerToBeStopped) {
		timeoutTimer->running = false;
	}
	else if (curlRequestsTimeoutTimerToTimeoutAsap) {
		timeoutTimer->interval.set(std::chrono::microseconds(1));
		timeoutTimer->running = true;
	}
	else {
		timeoutTimer->interval = std::chrono::milliseconds(timeoutMs);
		timeoutTimer->running = true;
	}

	return 0;
}

void NetworkAccessManager::onFileDescriptorNotifierTriggered(int nfd, FileDescriptorNotifier::NotificationType fdnType)
{
	spdlog::debug("NetworkAccessManager::onFileDescriptorNotifierTriggered() - fd:{}, {}", nfd, s_notificationTypeToString.at(fdnType));

	m_timeoutTimer.running = false;

	auto cselectFromFileDescriptorNotificationType = [](FileDescriptorNotifier::NotificationType fdnType) {
		switch (fdnType) {
		case FileDescriptorNotifier::NotificationType::Read:
			return CURL_CSELECT_IN;
		case FileDescriptorNotifier::NotificationType::Write:
			return CURL_CSELECT_OUT;
		default:
			return 0;
		}
	};

	auto rc = curl_multi_socket_action(m_handle, nfd, cselectFromFileDescriptorNotificationType(fdnType), &m_numberOfRunningTransfers);
	checkCurlMultiResultAndDoDebugPrints(rc);

	processTransferMessages();
}

void NetworkAccessManager::onTimeoutTimerTriggered()
{
	spdlog::debug("NetworkAccessManager::onTimeoutTimerTriggered()");

	auto rc = curl_multi_socket_action(m_handle, CURL_SOCKET_TIMEOUT, 0, &m_numberOfRunningTransfers);
	checkCurlMultiResultAndDoDebugPrints(rc);

	processTransferMessages();
}

void NetworkAccessManager::processTransferMessages()
{
	int numberOfMessagesLeft = 0;
	CURLMsg *msg;
	while ((msg = curl_multi_info_read(m_handle, &numberOfMessagesLeft))) {
		if (msg->msg == CURLMSG_DONE) {
			auto transferHandle = AbstractTransferHandle::fromCurlEasyHandle(msg->easy_handle);
			unregisterTransfer(transferHandle);

			transferHandle->finished.emit();
		}
	}
}

bool NetworkAccessManager::checkCurlMultiResultAndDoDebugPrints(CURLMcode c) const
{
	const auto isError = (c != CURLM_OK);
	if (isError) {
		spdlog::error("curl_multi function returned error {}", curl_multi_strerror(c));
	}
	return isError;
}

void NetworkAccessManager::FileDescriptorNotifierRegistry::manageFileDescriptorNotifiers(curl_socket_t socket, int eventType)
{
	// TODO -> translating CURL_POLL events to KDFoundation::FileDescriptorNotifiers with respective NotificationTypes can probalby be done more elegant than this, works for now though
	switch (eventType) {
	case CURL_POLL_IN:
		registerFileDescriptorNotifier(socket, readMap, FileDescriptorNotifier::NotificationType::Read);
		unregisterFileDescriptorNotifier(socket, writeMap, FileDescriptorNotifier::NotificationType::Write);
		break;
	case CURL_POLL_OUT:
		unregisterFileDescriptorNotifier(socket, readMap, FileDescriptorNotifier::NotificationType::Read);
		registerFileDescriptorNotifier(socket, writeMap, FileDescriptorNotifier::NotificationType::Write);
		break;
	case CURL_POLL_INOUT:
		registerFileDescriptorNotifier(socket, readMap, FileDescriptorNotifier::NotificationType::Read);
		registerFileDescriptorNotifier(socket, writeMap, FileDescriptorNotifier::NotificationType::Write);
		break;
	case CURL_POLL_REMOVE:
		unregisterFileDescriptorNotifier(socket, readMap, FileDescriptorNotifier::NotificationType::Read);
		unregisterFileDescriptorNotifier(socket, writeMap, FileDescriptorNotifier::NotificationType::Write);
		break;
	default:
		spdlog::warn("NetworkAccessManager::socketCallback() - called with unknown eventType");
		break;
	}
}

void NetworkAccessManager::FileDescriptorNotifierRegistry::registerFileDescriptorNotifier(int nfd, FileDescriptorNotifierMap &fdnMap, FileDescriptorNotifier::NotificationType fdnType)
{
	spdlog::debug("NetworkAccessManager::registerFileDescriptorNotifier() - fd:{}, {}", nfd, s_notificationTypeToString.at(fdnType));
	if (fdnMap.contains(nfd)) {
		return; // TODO -> with the present implementation this will happen on a regular basis and is not critical
	}

	fdnMap[nfd] = std::make_unique<FileDescriptorNotifier>(nfd, fdnType);
	fdnMap[nfd]->triggered.connect([this, nfd, fdnType]() { NetworkAccessManager::instance().onFileDescriptorNotifierTriggered(nfd, fdnType); });
}

void NetworkAccessManager::FileDescriptorNotifierRegistry::unregisterFileDescriptorNotifier(int nfd, FileDescriptorNotifierMap &fdnMap, FileDescriptorNotifier::NotificationType fdnType)
{
	spdlog::debug("NetworkAccessManager::unregisterFileDescriptorNotifier() - fd:{}, {}", nfd, s_notificationTypeToString.at(fdnType));
	if (!fdnMap.contains(nfd)) {
		return; // TODO -> with the present implementation this will happen on a regular basis and is not critical
	}

	fdnMap[nfd]->triggered.disconnectAll();
	fdnMap.erase(nfd);
}
