#include "mqtt.h"
#include <spdlog/spdlog.h>

constexpr std::chrono::milliseconds c_miscTaskInterval = std::chrono::milliseconds(1000);

using namespace KDFoundation;

MqttLib::MqttLib()
	: m_isInitialized{false}
	, m_mosquittoLib(&MosquittoLib::instance())
{
	
}

MqttLib &MqttLib::instance()
{
	static MqttLib s_instance;
	return s_instance;
}

int MqttLib::init()
{
	int result = MOSQ_ERR_UNKNOWN;

	if (!m_isInitialized) {
		result = m_mosquittoLib->init();
		const auto hasError = checkMosquittoResultAndDoDebugPrints(result, "MqttLib::init()");
		m_isInitialized = !hasError;
		if (m_isInitialized) {
			int major, minor, revision = 0;
			version(&major, &minor, &revision);
			spdlog::info("MqttLib::init() - using libmosquitto v{}.{}.{}", major, minor, revision);
		}
	} else {
		spdlog::warn("MqttLib::init() - Library is already initialized.");
	}
	return result;
}

int MqttLib::cleanup()
{
	const auto result = m_mosquittoLib->cleanup();
	const auto hasError = checkMosquittoResultAndDoDebugPrints(result, "MqttLib::cleanup()");
	m_isInitialized = hasError ? m_isInitialized : false;
	return result;
}

bool MqttLib::isInitialized() const
{
	return m_isInitialized;
}

bool MqttLib::isValidTopicNameForSubscription(const std::string &topic)
{
	return m_mosquittoLib->isValidTopicNameForSubscription(topic);
}

int MqttLib::version(int *major, int *minor, int *revision)
{
	return m_mosquittoLib->version(major, minor, revision);
}

bool MqttLib::checkMosquittoResultAndDoDebugPrints(int result, std::string_view func)
{
	const auto isError = (result != MOSQ_ERR_SUCCESS);
	if (isError) {
		const auto funcString = func.empty() ? "mosquitto function" : func;
		spdlog::error("{} - error: {}", funcString, errorString(result));
	}
	return isError;
}

std::string_view MqttLib::connackString(int connackCode)
{
	return m_mosquittoLib->connackString(connackCode);
}

std::string_view MqttLib::errorString(int errorCode)
{
	return m_mosquittoLib->errorString(errorCode);
}

std::string_view MqttLib::reasonString(int reasonCode)
{
	return m_mosquittoLib->reasonString(reasonCode);
}

MqttClient::MqttClient(const std::string &clientId, bool cleanSession, bool verbose)
	: m_verbose{verbose}
{
	if (!MqttLib::instance().isInitialized()) {
		spdlog::warn("MqttClient::MqttClient() - CTOR called before MqttLib::init(). Initialize lib before instantiating MqttClient object!");
	}

	auto *client = new MosquittoClient(clientId, cleanSession);
	m_mosquitto.init(client, this);

	m_eventLoopHook.init(c_miscTaskInterval, this);
}

int MqttClient::setTls(const File &cafile)
{
	spdlog::debug("MqttClient::setTls() - cafile: {}", cafile.path());

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::setTls() - Setting TLS is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (!cafile.exists()) {
		spdlog::error("MqttClient::setTls() - cafile does not exist.");
		return MOSQ_ERR_UNKNOWN;
	}

	std::optional<const std::string> nullopt;
	const auto result = m_mosquitto.client()->tlsSet(cafile.path(), nullopt, nullopt, nullopt);
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::setTls()");

	return result;
}

int MqttClient::setUsernameAndPassword(const std::string &username, const std::string &password)
{
	spdlog::debug("MqttClient::setUsernameAndPassword()");

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::setUsernameAndPassword() - Setting AUTH is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = m_mosquitto.client()->usernamePasswordSet(username, password);
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::setUsernameAndPassword()");
	return result;
}

int MqttClient::setWill(const std::string &topic, int payloadlen, const void *payload, int qos, bool retain)
{
	spdlog::debug("MqttClient::setWill() - topic:{}, qos:{}, retain:{})", topic, qos, retain);

	if (connectionState.get() != ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::setWill() - Setting will is only allowed when disconnected.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = m_mosquitto.client()->willSet(topic.c_str(), payloadlen, payload, qos, retain);
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::setWill()");
	return result;
}

int MqttClient::connect(const Url &host, int port, int keepalive)
{
	spdlog::debug("MqttClient::connect() - host:{}, port:{}, keepalive:{})", host.url(), port, keepalive);

	if (connectionState.get() == ConnectionState::CONNECTING) {
		spdlog::error("MqttClient::connect() - Already connecting to host.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (connectionState.get() == ConnectionState::CONNECTED) {
		spdlog::error("MqttClient::connect() - Already connected to a host. Disconnect from current host first.");
		return MOSQ_ERR_UNKNOWN;
	}

	connectionState.set(ConnectionState::CONNECTING);
	// TODO -> TLS only works with connect(), it does not work with connect_async()
	// I'm uncertain if there is a setup that allows for connect_async() with TLS (I didn't manage to find one)
	// (the use of non-blocking connect_async() would be preferred from our POV)
	// other people seem to have encountered similiar behaviour before, though this issue should have been fixed a while ago
	// -> https://github.com/eclipse/mosquitto/issues/990
	const auto start = clock();
	const auto result = m_mosquitto.client()->connect(host.url().c_str(), port, keepalive);
	const auto end = clock();
	const auto elapsedTimeMs = std::round((double(end-start) / double(CLOCKS_PER_SEC)) * 1000000.0);
	spdlog::info("MqttClient::connect() - blocking call of MosquittoClient::connect() took {} µs", elapsedTimeMs);

	const auto hasError = MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::connect()");
	if (!hasError) {
		m_eventLoopHook.engage(m_mosquitto.client()->socket());
	}
	return result;
}

int MqttClient::disconnect()
{
	spdlog::debug("MqttClient::disconnect()");

	if (connectionState.get() == ConnectionState::DISCONNECTING) {
		spdlog::error("MqttClient::disconnect() - Already diconnecting from host.");
		return MOSQ_ERR_UNKNOWN;
	}

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::disconnect() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	connectionState.set(ConnectionState::DISCONNECTING);
	const auto result = m_mosquitto.client()->disconnect();
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::disconnect()");
	return result;
}

int MqttClient::publish(int *msgId, const char *topic, int payloadlen, const void *payload, int qos, bool retain)
{
	spdlog::debug("MqttClient::publish() - topic:{}, qos:{}, retain:{}", topic, qos, retain);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::publish() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	const auto result = m_mosquitto.client()->publish(msgId, topic, payloadlen, payload, qos, retain);
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::publish()");
	return result;
}

int MqttClient::subscribe(const char *pattern, int qos)
{
	spdlog::debug("MqttClient::subscribe() - subscribe pattern:{}, qos:{}", pattern, qos);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::subscribe() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	int msgId;
	const auto result = m_mosquitto.client()->subscribe(&msgId, pattern, qos);
	const auto hasError = MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::subscribe()");
	if (!hasError) {
		const auto topic = std::string(pattern);
		m_subscriptionsRegistry.registerPendingRegistryOperation(topic, msgId);
		subscriptionState.set(SubscriptionState::SUBSCRIBING);
	}
	return result;
}

int MqttClient::unsubscribe(const char *pattern)
{
	spdlog::debug("MqttClient::unsubscribe() - unsubscribe pattern:{}", pattern);

	if (connectionState.get() == ConnectionState::DISCONNECTED) {
		spdlog::error("MqttClient::unsubscribe() - Not connected to any host.");
		return MOSQ_ERR_UNKNOWN;
	}

	int msgId;
	const auto result = m_mosquitto.client()->unsubscribe(&msgId, pattern);
	const auto hasError = MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "MqttClient::unsubscribe()");
	if (!hasError) {
		const auto topic = std::string(pattern);
		m_subscriptionsRegistry.registerPendingRegistryOperation(topic, msgId);
		subscriptionState.set(SubscriptionState::UNSUBSCRIBING);
	}
	return result;
}

void MqttClient::onConnected(int connackCode)
{
	spdlog::debug("MqttClient::onConnected() - connackCode({}): {}", connackCode, MqttLib::instance().connackString(connackCode));
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

	if (m_verbose) {
		const auto tlsIsEnabled = (m_mosquitto.client()->sslGet() != nullptr);
		spdlog::info("MqttClient::onConnected() - This connection {} TLS encrypted", tlsIsEnabled ? "is" : "is not");
	}

	const auto state = hasError ? ConnectionState::DISCONNECTED : ConnectionState::CONNECTED;
	connectionState.set(state);
}

void MqttClient::onDisconnected(int reasonCode)
{
	spdlog::debug("MqttClient::onDisconnected() - reasonCode({}): {}", reasonCode, MqttLib::instance().reasonString(reasonCode));

	m_eventLoopHook.disengage();

	connectionState.set(ConnectionState::DISCONNECTED);
}

void MqttClient::onPublished(int msgId)
{
	spdlog::debug("MqttClient::onPublished() - msgId:{}", msgId);
	msgPublished.emit(msgId);
}

void MqttClient::onMessage(const mosquitto_message *message)
{
	spdlog::debug("MqttClient::onMessage() - message.id:{}, message.topic:{}", message->mid, message->topic);
	msgReceived.emit(message);
}

void MqttClient::onSubscribed(int msgId, int qosCount, const int *grantedQos)
{
	// we only handle subscriptions to one single topic with one single QOS value for now.
	// in case mosquitto_subscribe_multiple is added to MosquittoClient some time in the future,
	// add handling of multiple topic/QOS pairs here.
	assert(qos_count == 1);

	const auto topic = m_subscriptionsRegistry.registerTopicSubscriptionAndReturnTopicName(msgId, grantedQos[0]);
	spdlog::debug("MqttClient::onSubscribed() - msgId:{}, topic:{}, qosCount:{}, grantedQos:{}", msgId, topic, qosCount, grantedQos[0]);

	const auto state = m_subscriptionsRegistry.subscribedTopics().empty() ? SubscriptionState::UNSUBSCRIBED : SubscriptionState::SUBSCRIBED;
	subscriptionState.set(state);
	subscriptions.set(m_subscriptionsRegistry.subscribedTopics());
}

void MqttClient::onUnsubscribed(int msgId)
{
	const auto topic = m_subscriptionsRegistry.unregisterTopicSubscriptionAndReturnTopicName(msgId);
	spdlog::debug("MqttClient::onUnsubscribed() - msgId:{}, topic:{}", msgId, topic);

	const auto state = m_subscriptionsRegistry.subscribedTopics().empty() ? SubscriptionState::UNSUBSCRIBED : SubscriptionState::SUBSCRIBED;
	subscriptionState.set(state);
	subscriptions.set(m_subscriptionsRegistry.subscribedTopics());
}

void MqttClient::onLog(int level, const char *str) const
{
	if (m_verbose) {
		spdlog::info("MqttClient::onLog() - level:{}, string:{})", level, str);
	}
}

void MqttClient::onError()
{
	spdlog::error("MqttClient::onError()");
	error.emit();
}

void MqttClient::onReadOpRequested()
{
	auto result = m_mosquitto.client()->loopRead();
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "loopRead()");
}

void MqttClient::onWriteOpRequested()
{
	const auto writeOpIsPending = m_mosquitto.client()->wantWrite();
	if (!writeOpIsPending) {
		return;
	}

	auto result = m_mosquitto.client()->loopWrite();
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "loopWrite()");
}

void MqttClient::onMiscTaskRequested()
{
	auto result = m_mosquitto.client()->loopMisc();
	MqttLib::instance().checkMosquittoResultAndDoDebugPrints(result, "loopMisc()");
}

void MqttClient::EventLoopHook::init(const std::chrono::milliseconds miscTaskInterval, MqttClient *parent)
{
	spdlog::debug("MqttClient::EventLoopHook::init()");
	assert(parent != nullptr);

	this->parent = parent;

	miscTaskTimer = std::make_unique<Timer>();
	miscTaskTimer->interval.set(miscTaskInterval);
	miscTaskTimer->running.set(false);
	miscTaskTimer->timeout.connect(&MqttClient::onMiscTaskRequested, parent);
}

void MqttClient::EventLoopHook::engage(const int socket)
{
	spdlog::debug("MqttClient::EventLoopHook::engage()");

	if (!isSetup()) {
		spdlog::error("MqttClient::EventLoopHook::engage() - EventLoopHook is not initialized. Call MqttClient::EventLoopHook::init() first.");
		return;
	}

	if (isEngaged()) {
		spdlog::error("MqttClient::EventLoopHook::engage() - Already engaged.");
		return;
	}

	if (socket < 0) {
		spdlog::error("MqttClient::EventLoopHook::engage() - Invalid socket.");
		return;
	}

	readOpNotifier = std::make_unique<FileDescriptorNotifier>(socket, FileDescriptorNotifier::NotificationType::Read);
	writeOpNotifier = std::make_unique<FileDescriptorNotifier>(socket, FileDescriptorNotifier::NotificationType::Write);

	readOpNotifier->triggered.connect(&MqttClient::onReadOpRequested, parent);
	writeOpNotifier->triggered.connect(&MqttClient::onWriteOpRequested, parent);

	miscTaskTimer->running.set(true);
}

void MqttClient::EventLoopHook::disengage()
{
	spdlog::debug("MqttClient::EventLoopHook::disengage()");

	if (!isEngaged()) {
		spdlog::error("MqttClient::EventLoopHook::disengage() - Already disengaged.");
		return;
	}

	miscTaskTimer->running.set(false);

	readOpNotifier->triggered.disconnectAll();
	writeOpNotifier->triggered.disconnectAll();

	readOpNotifier = {};
	writeOpNotifier = {};
}

bool MqttClient::EventLoopHook::isSetup() const
{
	return (miscTaskTimer && (parent != nullptr));
}

bool MqttClient::EventLoopHook::isEngaged() const
{
	return (readOpNotifier && writeOpNotifier);
}

void MqttClient::MosquittoClientDependency::init(MosquittoClient *client, MqttClient *parent)
{
	spdlog::debug("MqttClient::MosquittoClientDependency::init()");
	assert(parent != nullptr);

	delete mosquittoClient;
	mosquittoClient = client;

	mosquittoClient->connected.connect(&MqttClient::onConnected, parent);
	mosquittoClient->disconnected.connect(&MqttClient::onDisconnected, parent);
	mosquittoClient->published.connect(&MqttClient::onPublished, parent);
	mosquittoClient->message.connect(&MqttClient::onMessage, parent);
	mosquittoClient->subscribed.connect(&MqttClient::onSubscribed, parent);
	mosquittoClient->unsubscribed.connect(&MqttClient::onUnsubscribed, parent);
	mosquittoClient->log.connect(&MqttClient::onLog, parent);
	mosquittoClient->error.connect(&MqttClient::onError, parent);
}

MosquittoClient *MqttClient::MosquittoClientDependency::client()
{
	return mosquittoClient;
}

void MqttClient::SubscriptionsRegistry::registerPendingRegistryOperation(std::string_view topic, int msgId)
{
	spdlog::debug("MqttClient::SubscriptionsRegistry::registerPendingRegistryOperation() - topic:{}, msgId:{}", topic, msgId);
	topicByMsgIdOfPendingOperations[msgId] = topic;
}

std::string MqttClient::SubscriptionsRegistry::registerTopicSubscriptionAndReturnTopicName(int msgId, int grantedQos)
{
	spdlog::debug("MqttClient::SubscriptionsRegistry::registerTopicSubscriptionAndReturnTopicName() - msgId:{}, grantedQos:{}", msgId, grantedQos);

	auto it = topicByMsgIdOfPendingOperations.find(msgId);
	if (it == topicByMsgIdOfPendingOperations.end()) {
		spdlog::error("MqttClient::SubscriptionsRegistry::registerTopicSubscriptionAndReturnTopicName() - No pending operation with msgId: {}.", msgId);
		return {};
	}

	auto topic = it->second;
	topicByMsgIdOfPendingOperations.erase(it);

	qosByTopicOfActiveSubscriptions[topic] = grantedQos;

	return topic;
}

std::string MqttClient::SubscriptionsRegistry::unregisterTopicSubscriptionAndReturnTopicName(int msgId)
{
	spdlog::debug("MqttClient::SubscriptionsRegistry::unregisterTopicSubscriptionAndReturnTopicName() - msgId:{}", msgId);
	
	auto it = topicByMsgIdOfPendingOperations.find(msgId);
	if (it == topicByMsgIdOfPendingOperations.end()) {
		spdlog::error("MqttClient::SubscriptionsRegistry::unregisterTopicSubscriptionAndReturnTopicName() - No pending operation with msgId: {}.", msgId);
		return {};
	}

	const auto &topic = it->second;
	topicByMsgIdOfPendingOperations.erase(it);

	qosByTopicOfActiveSubscriptions.erase(topic);

	return topic;
}

std::vector<std::string> MqttClient::SubscriptionsRegistry::subscribedTopics() const
{
	std::vector<std::string> keys;
	keys.reserve(qosByTopicOfActiveSubscriptions.size());
	for (const auto &pair : qosByTopicOfActiveSubscriptions) {
		keys.push_back(pair.first);
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

int MqttClient::SubscriptionsRegistry::grantedQosForTopic(const std::string &topic) const
{
	return qosByTopicOfActiveSubscriptions.at(topic);
}
