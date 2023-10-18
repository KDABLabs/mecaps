#include "mqtt.h"
#include <spdlog/spdlog.h>

constexpr std::chrono::milliseconds miscTaskTimerInterval = std::chrono::milliseconds(1000);

using namespace KDFoundation;
using namespace mosqpp;

bool MQTT::s_isLibInitialized = false;

int MQTT::libInit()
{
	int result = MOSQ_ERR_UNKNOWN;

	if (!s_isLibInitialized) {
		const auto result = mosqpp::lib_init();
		const auto hasError = checkMosquitoppResultAndDoDebugPrints(result, "MQTT::libInit()");
		s_isLibInitialized = hasError ? s_isLibInitialized : true;
	} else {
		spdlog::warn("MQTT::libInit() - Library is already initialized.");
	}
	return result;
}

int MQTT::libCleanup()
{
	const auto result = mosqpp::lib_cleanup();
	const auto hasError = checkMosquitoppResultAndDoDebugPrints(result, "MQTT::libCleanup()");
	s_isLibInitialized = hasError ? s_isLibInitialized : false;
	return result;
}

MQTT::MQTT(const std::string &clientId, bool cleanSession, bool verbose)
	: m_verbose{verbose}
{
	if (!s_isLibInitialized) {
		spdlog::warn("MQTT::MQTT() - CTOR called before MQTT::libInit(). Initialize lib before instantiating MQTT object!");
	}

	if (m_verbose) {
		int major, minor, revision = 0;
		mosqpp::lib_version(&major, &minor, &revision);
		spdlog::info("MQTT::MQTT() - using libmosquitto v{}.{}.{}", major, minor, revision);

//		TODO -> add as soon as get_ssl() is added to mosquittopp API
//		const auto sslStruct = mosquittopp::get_ssl();
//		spdlog::info("MQTT::MQTT() - TLS connections are {}", sslStruct ? "enabled" : "not enabled");
	}
}

MQTT::~MQTT()
{

}

int MQTT::setTls(const File &cafile)
{
	spdlog::debug("MQTT::setTls() - cafile: {}", cafile.path());

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::setTls() - Setting TLS is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (!cafile.exists()) {
		spdlog::error("MQTT::setTls() - cafile does not exist.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = mosquittopp::tls_set(cafile.path().c_str());
	checkMosquitoppResultAndDoDebugPrints(result, "MQTT::setTls()");
	return result;
}

int MQTT::setUsernameAndPassword(const std::string &username, const std::string &password)
{
	spdlog::debug("MQTT::setUsernameAndPassword()");

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::setUsernameAndPassword() - Setting AUTH is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = mosquittopp::username_pw_set(username.c_str(), password.c_str());
	checkMosquitoppResultAndDoDebugPrints(result, "MQTT::setUsernameAndPassword()");
	return result;
}

int MQTT::setWill(const std::string &topic, int payloadlen, const void *payload, int qos, bool retain)
{
	spdlog::debug("MQTT::setWill() - topic:{}, qos:{}, retain:{})", topic, qos, retain);

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::setWill() - Setting will is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = mosquittopp::will_set(topic.c_str(), payloadlen, payload, qos, retain);
	checkMosquitoppResultAndDoDebugPrints(result, "MQTT::setWill()");
	return result;
}

int MQTT::connect(const std::string &host, int port, int keepalive)
{
	spdlog::debug("MQTT::connect() - host:{}, port:{}, keepalive:{})", host, port, keepalive);

	if (connectionState.get() == ConnectionState::CONNECTING) {
		spdlog::error("MQTT::connect() - Already connecting to host.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (connectionState.get() == ConnectionState::CONNECTED) {
		spdlog::error("MQTT::connect() - Already connected to a host. Disconnect from current host first.");
		return MOSQ_ERR_UNKNOWN;
	}

	connectionState.set(ConnectionState::CONNECTING);
	// TODO -> TLS only works with connect(), it does not work with connect_async()
	// I'm uncertain if there is a setup that allows for connect_async() with TLS (I didn't manage to find one)
	// (the use of non-blocking connect_async() would be preferred from our POV)
	// other people seem to have encountered similiar behaviour before, though this issue should have been fixed a while ago
	// -> https://github.com/eclipse/mosquitto/issues/990
	const auto start = clock();
	const auto result = mosquittopp::connect(host.c_str(), port, keepalive);
	const auto end = clock();
	const auto elapsedTimeMs = std::round((double(end-start) / double(CLOCKS_PER_SEC)) * 1000000.0);
	spdlog::info("MQTT::connect() - blocking call of mosquittopp::connect() took {} µs", elapsedTimeMs);

	const auto hasError = checkMosquitoppResultAndDoDebugPrints(result, "MQTT::connect()");
	if (!hasError) {
		hookToEventLoop();
	}
	return result;
}

int MQTT::disconnect()
{
	spdlog::debug("MQTT::disconnect()");

	if (connectionState.get() == ConnectionState::DISCONNECTING) {
		spdlog::error("MQTT::connect() - Already diconnecting from host.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::disconnect() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	connectionState.set(ConnectionState::DISCONNECTING);
	const auto result = mosquittopp::disconnect();
	checkMosquitoppResultAndDoDebugPrints(result, "MQTT::disconnect()");
	return result;
}

int MQTT::publish(int *msgId, const char *topic, int payloadlen, const void *payload, int qos, bool retain)
{
	spdlog::debug("MQTT::publish() - topic:{}, qos:{}, retain:{}", topic, qos, retain);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::publish() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = mosquittopp::publish(msgId, topic, payloadlen, payload, qos, retain);
	checkMosquitoppResultAndDoDebugPrints(result, "MQTT::publish()");
	return result;
}

int MQTT::subscribe(const char *pattern, int qos)
{
	spdlog::debug("MQTT::subscribe() - subscribe pattern:{}, qos:{}", pattern, qos);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::subscribe() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	int msgId;
	const auto result = mosquittopp::subscribe(&msgId, pattern, qos);
	const auto hasError = checkMosquitoppResultAndDoDebugPrints(result, "MQTT::subscribe()");
	if (!hasError) {
		const auto topic = std::string(pattern);
		m_subscriptionsRegistry.registerPendingRegistryOperation(topic, msgId);
		subscriptionState.set(SubscriptionState::SUBSCRIBING);
	}
	return result;
}

int MQTT::unsubscribe(const char *pattern)
{
	spdlog::debug("MQTT::unsubscribe() - unsubscribe pattern:{}", pattern);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MQTT::unsubscribe() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	int msgId;
	const auto result = mosquittopp::unsubscribe(&msgId, pattern);
	const auto hasError = checkMosquitoppResultAndDoDebugPrints(result, "MQTT::unsubscribe()");
	if (!hasError) {
		const auto topic = std::string(pattern);
		m_subscriptionsRegistry.registerPendingRegistryOperation(topic, msgId);
		subscriptionState.set(SubscriptionState::UNSUBSCRIBING);
	}
	return result;
}

bool MQTT::isValidTopicNameForSubscription(std::string_view topic)
{
	// TODO -> in case adding mosqpp::sub_topic_check() is accepted upstream, use it here
	const auto result = validate_utf8(topic.data(), topic.length());
	return (result == MOSQ_ERR_SUCCESS);
}

void MQTT::on_connect(int connackCode)
{
	spdlog::debug("MQTT::on_connect() - connackCode({}): {}", connackCode, mosqpp::connack_string(connackCode));
	const auto hasError = (connackCode != 0);
	if (hasError) {
		// TODO -> I'm uncertain if calling unhookFromEventLoop() here is perfectly fine in every case
		// I noticed on_diconnect (sometimes) gets called after on_connect was called with CONNACK!=0
		// in this case we may want to stay hooked to the event loop until we're finally disconnected
		// and on_disconnect is called.
		// For now I won't call unhookFromEventLoop() here and see if we ever run into a sw-path were
		// we never unhook from event loop. In this case we would need to add the following call here (only for certain connackCodes):
		// unhookFromEventLoop();
	}
	const auto state = hasError ? ConnectionState::DISCONNECTED : ConnectionState::CONNECTED;
	connectionState.set(state);
}

void MQTT::on_disconnect(int reasonCode)
{
	// TODO -> in case adding mosqpp::reason_string() is accepted upstream, use it here
	spdlog::debug("MQTT::on_disconnect() - reasonCode({})", reasonCode);

	unhookFromEventLoop();

	connectionState.set(ConnectionState::DISCONNECTED);
}

void MQTT::on_publish(int msgId)
{
	spdlog::debug("MQTT::on_publish() - msgId:{}", msgId);
	msgPublished.emit(msgId);
}

void MQTT::on_message(const mosquitto_message *message)
{
	spdlog::debug("MQTT::on_message() - message.id:{}, message.topic:{}", message->mid, message->topic);
	msgReceived.emit(message);
}

void MQTT::on_subscribe(int msgId, int qos_count, const int *granted_qos)
{
	// mosquitto_subscribe_multiple is not part of the C++ API mosquittopp
	// thus we only handle subscriptions to one single topic with one single QOS value for now.
	// in case mosquitto_subscribe_multiple is added to mosquittopp some time in the future,
	// add handling of multiple topic/QOS pairs here.
	assert(qos_count==1);

	const auto topic = m_subscriptionsRegistry.registerTopicSubscriptionAndReturnTopicName(msgId, granted_qos[0]);
	spdlog::debug("MQTT::on_subscribe() - msgId:{}, topic:{}, qosCount:{}, grantedQos:{}", msgId, topic, qos_count, granted_qos[0]);

	const auto state = m_subscriptionsRegistry.subscribedTopics().empty() ? SubscriptionState::UNSUBSCRIBED : SubscriptionState::SUBSCRIBED;
	subscriptionState.set(state);
	subscriptions.set(m_subscriptionsRegistry.subscribedTopics());
}

void MQTT::on_unsubscribe(int msgId)
{
	const auto topic = m_subscriptionsRegistry.unregisterTopicSubscriptionAndReturnTopicName(msgId);
	spdlog::debug("MQTT::on_unsubscribe() - msgId:{}, topic:{}", msgId, topic);

	const auto state = m_subscriptionsRegistry.subscribedTopics().empty() ? SubscriptionState::UNSUBSCRIBED : SubscriptionState::SUBSCRIBED;
	subscriptionState.set(state);
	subscriptions.set(m_subscriptionsRegistry.subscribedTopics());
}

void MQTT::on_log(int level, const char *str)
{
	if (m_verbose) {
		spdlog::info("MQTT::on_log() - level:{}, string:{})", level, str);
	}
}

void MQTT::on_error()
{
	spdlog::error("MQTT::on_error()");
	error.emit();
}

void MQTT::hookToEventLoop()
{
	spdlog::debug("MQTT::hookToEventLoop()");

	if (m_fdnRead || m_fdnWrite || m_miscTaskTimer) {
		spdlog::error("MQTT::hookToEventLoop() - Already hooked to event loop.");
		return;
	}

	const auto socket = mosquittopp::socket();

	m_fdnRead = std::make_unique<FileDescriptorNotifier>(socket, FileDescriptorNotifier::NotificationType::Read);
	m_fdnWrite = std::make_unique<FileDescriptorNotifier>(socket, FileDescriptorNotifier::NotificationType::Write);

	m_fdnRead->triggered.connect([this]() { onFileDescriptorNotifierTriggeredRead(); } );
	m_fdnWrite->triggered.connect([this]() { onFileDescriptorNotifierTriggeredWrite(); } );

	m_miscTaskTimer = std::make_unique<Timer>();
	m_miscTaskTimer->interval.set(miscTaskTimerInterval);
	m_miscTaskTimer->timeout.connect([this]() { onMiscTaskTimerTriggered(); } );
	m_miscTaskTimer->running.set(true);
}

void MQTT::unhookFromEventLoop()
{
	spdlog::debug("MQTT::unhookFromEventLoop()");

	if (!(m_fdnRead && m_fdnWrite && m_miscTaskTimer)) {
		spdlog::error("MQTT::hookToEventLoop() - Already unhooked from event loop.");
		return;
	}

	m_fdnRead->triggered.disconnectAll();
	m_fdnWrite->triggered.disconnectAll();
	m_miscTaskTimer->timeout.disconnectAll();

	m_fdnRead = {};
	m_fdnWrite = {};
	m_miscTaskTimer = {};
}

void MQTT::onFileDescriptorNotifierTriggeredRead()
{
	auto result = loop_read();
	checkMosquitoppResultAndDoDebugPrints(result, "loop_read()");
}

void MQTT::onFileDescriptorNotifierTriggeredWrite()
{
	const auto writeOpIsPending = mosquittopp::want_write();
	if (!writeOpIsPending) {
		return;
	}

	auto result = loop_write();
	checkMosquitoppResultAndDoDebugPrints(result, "loop_write()");
}

void MQTT::onMiscTaskTimerTriggered()
{
	auto result = loop_misc();
	checkMosquitoppResultAndDoDebugPrints(result, "loop_misc()");
}

bool MQTT::checkMosquitoppResultAndDoDebugPrints(int result, std::string_view func)
{
	const auto isError = (result != MOSQ_ERR_SUCCESS);
	if (isError) {
		const auto funcString = func.empty() ? "mosquittopp function" : func;
		const auto errorString = mosqpp::strerror(result);
		spdlog::error("{} - error: {}", funcString, errorString);
	}
	return isError;
}

void MQTT::SubscriptionsRegistry::registerPendingRegistryOperation(std::string_view topic, int msgId)
{
	topicByMsgIdOfPendingOperations[msgId] = topic;
}

std::string MQTT::SubscriptionsRegistry::registerTopicSubscriptionAndReturnTopicName(int msgId, int grantedQos)
{
	auto it = topicByMsgIdOfPendingOperations.find(msgId);
	if (it == topicByMsgIdOfPendingOperations.end()) {
		spdlog::error("MQTT::SubscriptionsRegistry::registerTopicSubscriptionAndReturnTopicName() - No pending operation with msgId: {}.", msgId);
		return {};
	}

	const auto topic = it->second;
	topicByMsgIdOfPendingOperations.erase(it);

	qosByTopicOfActiveSubscriptions[topic] = grantedQos;

	return topic;
}

std::string MQTT::SubscriptionsRegistry::unregisterTopicSubscriptionAndReturnTopicName(int msgId)
{
	auto it = topicByMsgIdOfPendingOperations.find(msgId);
	if (it == topicByMsgIdOfPendingOperations.end()) {
		spdlog::error("MQTT::SubscriptionsRegistry::unregisterTopicSubscriptionAndReturnTopicName() - No pending operation with msgId: {}.", msgId);
		return {};
	}

	const auto topic = it->second;
	topicByMsgIdOfPendingOperations.erase(it);

	qosByTopicOfActiveSubscriptions.erase(topic);

	return topic;
}

std::vector<std::string> MQTT::SubscriptionsRegistry::subscribedTopics() const
{
	std::vector<std::string> keys;
	for (const auto &pair : qosByTopicOfActiveSubscriptions) {
		keys.push_back(pair.first);
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

int MQTT::SubscriptionsRegistry::grantedQosForTopic(const std::string &topic) const
{
	return qosByTopicOfActiveSubscriptions.at(topic);
}
