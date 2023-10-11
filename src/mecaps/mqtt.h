#pragma once

#include <KDFoundation/file_descriptor_notifier.h>
#include "KDFoundation/timer.h"
#include <mosquittopp.h>

using namespace KDFoundation;

class MQTT : private mosqpp::mosquittopp
{
  public:
	enum class ConnectionState {
		CONNECTING,
		CONNECTED,
		DISCONNECTING,
		DISCONNECTED
	};

	enum class SubscriptionState {
		SUBSCRIBING,
		SUBSCRIBED,
		UNSUBSCRIBING,
		UNSUBSCRIBED
	};

	static int libInit();
	static int libCleanup();

	MQTT(const std::string &clientId, bool cleanSession = true, bool verbose = false);
	~MQTT();

	KDBindings::Property<ConnectionState> connectionState { ConnectionState::DISCONNECTED };
	KDBindings::Property<SubscriptionState> subscriptionState { SubscriptionState::UNSUBSCRIBED };

	KDBindings::Property<std::vector<std::string>> subscriptions { };

	KDBindings::Signal<int> msgPublished;
	KDBindings::Signal<const mosquitto_message *> msgReceived;

	KDBindings::Signal<> error;

	int setUsernameAndPassword(const std::string &username, const std::string &password);

	int connect(const std::string &host, int port = 1883, int keepalive = 60);
	int disconnect();

	int publish(int *msgId, const char *topic, int payloadlen = 0, const void *payload = NULL, int qos = 0, bool retain = false);

	int subscribe(const char *pattern, int qos = 0);
	int unsubscribe(const char *pattern);

	static bool isValidTopicNameForSubscription(std::string_view topic);

  private:
	virtual void on_connect(int connackCode) override;
//	virtual void on_connect_with_flags(int returnCode, int flags) override;
	virtual void on_disconnect(int reasonCode) override;
	virtual void on_publish(int msgId) override;
	virtual void on_message(const struct mosquitto_message *message) override;
	virtual void on_subscribe(int msgId, int qos_count, const int *granted_qos) override;
	virtual void on_unsubscribe(int msgId) override;
	virtual void on_log(int level, const char *str) override;
	virtual void on_error() override;

	void hookToEventLoop();
	void unhookFromEventLoop();

	void onFileDescriptorNotifierTriggeredRead();
	void onFileDescriptorNotifierTriggeredWrite();
	void onMiscTaskTimerTriggered();

	static bool checkMosquitoppResultAndDoDebugPrints(int result, std::string_view func = {});

	std::unique_ptr<FileDescriptorNotifier> m_fdnRead;
	std::unique_ptr<FileDescriptorNotifier> m_fdnWrite;
	std::unique_ptr<Timer> m_miscTaskTimer;
	bool m_verbose;

	static bool s_isLibInitialized;

	struct SubscriptionsRegistry
	{
		void registerPendingRegistryOperation(std::string_view topic, int msgId);
		std::string registerTopicSubscriptionAndReturnTopicName(int msgId, int grantedQos);
		std::string unregisterTopicSubscriptionAndReturnTopicName(int msgId);

		std::vector<std::string> subscribedTopics() const;
		int grantedQosForTopic(const std::string &topic) const;

		std::unordered_map<std::string,int> qosByTopicOfActiveSubscriptions;
		std::unordered_map<int,std::string> topicByMsgIdOfPendingOperations;
	};
	SubscriptionsRegistry m_subscriptionsRegistry;
};
