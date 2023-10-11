#include "application_engine.h"
#include "ftp_transfer_handle.h"
#include "http_transfer_handle.h"
#include "mqtt.h"
#include "network_access_manager.h"
#include <spdlog/spdlog.h>

ApplicationEngine &ApplicationEngine::init(const slint::ComponentHandle<AppWindow> &appWindow)
{
	static ApplicationEngine s_instance(appWindow);
	return s_instance;
}

ApplicationEngine::ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow)
{
	InitCounterDemo(appWindow->global<CounterSingleton>());
	InitHttpDemo(appWindow->global<HttpSingleton>());
	InitFtpDemo(appWindow->global<FtpSingleton>());
	InitMqttDemo(appWindow->global<MqttSingleton>());
}

ApplicationEngine::~ApplicationEngine()
{
	DeinitMqttDemo();
}

void ApplicationEngine::InitCounterDemo(const CounterSingleton &uiPageCounter)
{
	auto incrementCounterValue = [&]() {
		const auto newCounterValue = uiPageCounter.get_counter() + 1;
		uiPageCounter.set_counter(newCounterValue);
	};
	uiPageCounter.on_request_increase_value(incrementCounterValue);
}

void ApplicationEngine::InitHttpDemo(const HttpSingleton &httpSingleton)
{
	static auto &uiPageHttp = httpSingleton;
	static HttpTransferHandle *httpTransfer = nullptr;

	uiPageHttp.set_url("https://example.com");

	auto onHttpTransferFinished = [&](int result) {
		spdlog::info("HttpTransferHandle::finished() signal triggered for URL {}", httpTransfer->url());
		const auto fetchedContent = (result == CURLcode::CURLE_OK) ? httpTransfer->dataRead() : std::string();
		uiPageHttp.set_fetched_content(slint::SharedString(fetchedContent));
		delete httpTransfer;
	};

	auto startHttpQuery = [&]() {
		const std::string &url = uiPageHttp.get_url().data();
		httpTransfer = new HttpTransferHandle(url, true);
		httpTransfer->finished.connect(onHttpTransferFinished);
		NetworkAccessManager::instance().registerTransfer(httpTransfer);
	};
	uiPageHttp.on_request_http_query(startHttpQuery);
}

void ApplicationEngine::InitFtpDemo(const FtpSingleton &ftpSingleton)
{
	static auto &uiPageFtp = ftpSingleton;
	static FtpDownloadTransferHandle *ftpDownloadTransfer = nullptr;
	static FtpUploadTransferHandle *ftpUploadTransfer = nullptr;

	static File ftpFile = File("ls-lR.gz");
	uiPageFtp.set_url_download("ftp://ftp-stud.hs-esslingen.de/debian/ls-lR.gz");
	uiPageFtp.set_url_upload("ftp://ftp.cs.brown.edu/incoming/ls-lR.gz");

	auto onFtpExampleDownloadTransferFinished = [&]() {
		spdlog::info("FtpDownloadTransferHandle::finished() - downloaded {} bytes", ftpDownloadTransfer->numberOfBytesTransferred.get());
		uiPageFtp.set_is_downloading(false);
		delete ftpDownloadTransfer;
	};

	auto onFtpExampleDownloadTransferProgressPercentChanged = [&](const int &progressPercent) {
		uiPageFtp.set_progress_percent_download(progressPercent);
	};

	auto startFtpDownload = [&]() {
		const std::string &url = uiPageFtp.get_url_download().data();
		ftpDownloadTransfer = new FtpDownloadTransferHandle(ftpFile, url, false);
		ftpDownloadTransfer->finished.connect(onFtpExampleDownloadTransferFinished);
		ftpDownloadTransfer->progressPercent.valueChanged().connect(onFtpExampleDownloadTransferProgressPercentChanged);
		NetworkAccessManager::instance().registerTransfer(ftpDownloadTransfer);
		uiPageFtp.set_is_downloading(true);
	};
	uiPageFtp.on_request_ftp_download( [&] { startFtpDownload(); } );

	auto onFtpExampleUploadTransferFinished = [&]() {
		spdlog::info("FtpUploadTransferHandle::finished() - uploaded {} bytes", ftpUploadTransfer->numberOfBytesTransferred.get());
		uiPageFtp.set_is_uploading(false);
		delete ftpUploadTransfer;
	};

	auto onFtpExampleUploadTransferProgressPercentChanged = [&](const int &progressPercent) {
		uiPageFtp.set_progress_percent_upload(progressPercent);
	};

	auto startFtpUpload = [&]() {
		const std::string &url = uiPageFtp.get_url_upload().data();
		ftpUploadTransfer = new FtpUploadTransferHandle(ftpFile, url, true);
		ftpUploadTransfer->finished.connect(onFtpExampleUploadTransferFinished);
		ftpUploadTransfer->progressPercent.valueChanged().connect(onFtpExampleUploadTransferProgressPercentChanged);
		NetworkAccessManager::instance().registerTransfer(ftpUploadTransfer);
		uiPageFtp.set_is_uploading(true);
	};
	uiPageFtp.on_request_ftp_upload( [&] { startFtpUpload(); } );
}

void ApplicationEngine::InitMqttDemo(const MqttSingleton &mqttSingleton)
{
	MQTT::libInit();

	static auto &uiPageMqtt = mqttSingleton;
	static MQTT mqttClient = MQTT("mecapitto", true, true);

	static std::shared_ptr<slint::VectorModel<slint::SharedString>> mqttSubscriptionsModel(new slint::VectorModel<slint::SharedString>);
	uiPageMqtt.set_subscribed_topics(mqttSubscriptionsModel);

	uiPageMqtt.set_username("ro");
	uiPageMqtt.set_password("readonly");
	uiPageMqtt.set_url("test.mosquitto.org");
	uiPageMqtt.set_port(1883);
	uiPageMqtt.set_topic("my_test_topic");
	uiPageMqtt.set_payload("DEADBEEF");

	auto mqttTopicValidator = [&]() {
		const auto topic = uiPageMqtt.get_topic().data();
		const auto isValid = MQTT::isValidTopicNameForSubscription(topic);
		uiPageMqtt.set_is_topic_valid(isValid);
	};
	uiPageMqtt.on_user_edited_topic( [&] { mqttTopicValidator(); } );
	mqttTopicValidator(); // initial validation of default topic set above

	auto translateConnectionState = [](MQTT::ConnectionState connectionState) -> MqttConnectionState {
		switch (connectionState) {
		case MQTT::ConnectionState::CONNECTING: return MqttConnectionState::Connecting;
		case MQTT::ConnectionState::CONNECTED: return MqttConnectionState::Connected;
		case MQTT::ConnectionState::DISCONNECTING: return MqttConnectionState::Disconnecting;
		case MQTT::ConnectionState::DISCONNECTED: return MqttConnectionState::Disconnected;
		default:
			spdlog::error("Cannot translate MQTT::ConnectionState {} to MqttConnectionState!");
			return MqttConnectionState::Disconnected;
		}
	};

	auto translateSubscriptionState = [](MQTT::SubscriptionState subscriptionState) -> MqttSubscriptionState {
		switch (subscriptionState) {
		case MQTT::SubscriptionState::SUBSCRIBING: return MqttSubscriptionState::Subscribing;
		case MQTT::SubscriptionState::SUBSCRIBED: return MqttSubscriptionState::Subscribed;
		case MQTT::SubscriptionState::UNSUBSCRIBING: return MqttSubscriptionState::Unsubscribing;
		case MQTT::SubscriptionState::UNSUBSCRIBED: return MqttSubscriptionState::Unsubscribed;
		default:
			spdlog::error("Cannot translate MQTT::SubscriptionState {} to MqttSubscriptionState!");
			return MqttSubscriptionState::Unsubscribed;
		}
	};

	auto onMqttConnectionStateChanged = [&](const MQTT::ConnectionState &connectionState) {
		uiPageMqtt.set_connection_state(translateConnectionState(connectionState));
	};
	mqttClient.connectionState.valueChanged().connect(onMqttConnectionStateChanged);

	auto onMqttSubscriptionStateChanged = [&](const MQTT::SubscriptionState &subscriptionState) {
		uiPageMqtt.set_subscription_state(translateSubscriptionState(subscriptionState));
	};
	mqttClient.subscriptionState.valueChanged().connect(onMqttSubscriptionStateChanged);

	auto onMqttSubscriptionsChanged = [&] (const std::vector<std::string> &subscriptions) {
		const auto count = mqttSubscriptionsModel->row_count();
		for (int i = 0; i < count; ++i) {
			mqttSubscriptionsModel->erase(i);
		}
		for (const auto &topic : subscriptions) {
			mqttSubscriptionsModel->push_back(slint::SharedString(topic));
		}
	};
	mqttClient.subscriptions.valueChanged().connect(onMqttSubscriptionsChanged);

	auto onMqttMessageReceived = [&](const mosquitto_message *message) {
		const auto timestamp = std::time(nullptr);
		const auto timestring = std::string(std::asctime(std::localtime(&timestamp)));
		const auto topic = std::string(message->topic);
		const auto payload = std::string(static_cast<char*>(message->payload));
		uiPageMqtt.set_message(slint::SharedString(timestring.substr(0, timestring.size()-1) + " - " + topic + " - " + payload));
	};
	mqttClient.msgReceived.connect(onMqttMessageReceived);

	auto connectToMqttBroker = [&]() {
		const auto setUsernameAndPasswordOnConnect = uiPageMqtt.get_set_username_and_password_on_connect();
		const std::string username = setUsernameAndPasswordOnConnect ? uiPageMqtt.get_username().data() : std::string();
		const std::string password = setUsernameAndPasswordOnConnect ? uiPageMqtt.get_password().data() : std::string();

		const std::string url = uiPageMqtt.get_url().data();
		const auto port = uiPageMqtt.get_port();

		mqttClient.setUsernameAndPassword(username, password);
		mqttClient.connect(url, port, 10);
	};
	uiPageMqtt.on_request_mqtt_connect( [&] { connectToMqttBroker(); });

	auto disconnectFromMqttBroker = [&]() {
		mqttClient.disconnect();
	};
	uiPageMqtt.on_request_mqtt_disconnect( [&] { disconnectFromMqttBroker(); });

	auto subscribeToMqttTopic = [&]() {
		const auto topic = uiPageMqtt.get_topic().data();
		mqttClient.subscribe(topic);
	};
	uiPageMqtt.on_request_mqtt_subscribe( [&] { subscribeToMqttTopic(); } );

	auto unsubscribeFromMqttTopic = [&](slint::SharedString topic) {
		mqttClient.unsubscribe(topic.data());
	};
	uiPageMqtt.on_request_mqtt_unsubscribe( [&](slint::SharedString topic) { unsubscribeFromMqttTopic(topic); } );

	auto publishMqttMessage = [&]() {
		const auto topic = uiPageMqtt.get_topic().data();
		const std::string payload = uiPageMqtt.get_payload().data();
		mqttClient.publish(NULL, topic, payload.length(), payload.c_str());
	};
	uiPageMqtt.on_request_mqtt_publish( [&] { publishMqttMessage(); } );
}

void ApplicationEngine::DeinitMqttDemo()
{
	MQTT::libCleanup();
}
