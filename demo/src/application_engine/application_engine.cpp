#include "application_engine.h"
#ifdef CURL_AVAILABLE
#include "ftp_transfer_handle.h"
#include "http_transfer_handle.h"
#endif
#include <spdlog/spdlog.h>

ApplicationEngine &ApplicationEngine::init(const slint::ComponentHandle<AppWindow> &appWindow)
{
	static ApplicationEngine s_instance(appWindow);
	return s_instance;
}

ApplicationEngine::ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow)
{
	const auto &appSingleton = appWindow->global<AppSingleton>();

#ifdef SPDLOG_ACTIVE_LEVEL
	spdlog::set_level(spdlog::level::level_enum(SPDLOG_ACTIVE_LEVEL));
#endif

	InitCounterDemo(appWindow->global<CounterSingleton>());

#ifdef CURL_AVAILABLE
	const auto &networkAccessManager = NetworkAccessManager::instance();
	InitHttpDemo(appWindow->global<HttpSingleton>(), networkAccessManager);
	InitFtpDemo(appWindow->global<FtpSingleton>(), networkAccessManager);
#else
	appSingleton.set_curl_available(false);
#endif

#ifdef MOSQUITTO_AVAILABLE
	MqttLib::instance().init();
	static MqttClient mqttClient = MqttClient("mecapitto", true, true);
	InitMqttDemo(appWindow->global<MqttSingleton>(), mqttClient);
#else
	appSingleton.set_mosquitto_available(false);
#endif

}

ApplicationEngine::~ApplicationEngine()
{
#ifdef MOSQUITTO_AVAILABLE
	MqttLib::instance().cleanup();
#endif
}

void ApplicationEngine::InitCounterDemo(const CounterSingleton &uiPageCounter)
{
	auto incrementCounterValue = [&]() {
		const auto newCounterValue = uiPageCounter.get_counter() + 1;
		uiPageCounter.set_counter(newCounterValue);
	};
	uiPageCounter.on_request_increase_value(incrementCounterValue);
}

void ApplicationEngine::InitHttpDemo(const HttpSingleton &httpSingleton, const INetworkAccessManager &networkAccessManager)
{
#ifdef CURL_AVAILABLE
	httpSingleton.set_url("https://example.com");
	auto startHttpQuery = [&]() {
		const auto url = Url(httpSingleton.get_url().data());
		auto *httpTransfer = new HttpTransferHandle(url, true);

		httpTransfer->finished.connect([=, &httpSingleton](int result) {
			const auto fetchedContent =
				(result == CURLcode::CURLE_OK)
					? slint::SharedString(httpTransfer->dataRead())
					: slint::SharedString("Download failed");
			httpSingleton.set_fetched_content(fetchedContent);
			httpTransfer->deleteLater();
		});

		networkAccessManager.registerTransfer(*httpTransfer);
	};
	httpSingleton.on_request_http_query(startHttpQuery);
#endif
}

void ApplicationEngine::InitFtpDemo(const FtpSingleton &ftpSingleton, const INetworkAccessManager &networkAccessManager)
{
#ifdef CURL_AVAILABLE
	static File ftpFile = File("ls-lR.gz");
	ftpSingleton.set_url_download("ftp://ftp-stud.hs-esslingen.de/debian/ls-lR.gz");
	ftpSingleton.set_url_upload("ftp://ftp.cs.brown.edu/incoming/ls-lR.gz");

	auto startFtpDownload = [&]() {
		const auto &url = Url(ftpSingleton.get_url_download().data());
		auto ftpDownloadTransfer = new FtpDownloadTransferHandle(ftpFile, url, false);

		ftpDownloadTransfer->finished.connect([=, &ftpSingleton]() {
			spdlog::info("FtpDownloadTransferHandle::finished() - downloaded {} bytes", ftpDownloadTransfer->numberOfBytesTransferred.get());
			ftpSingleton.set_is_downloading(false);
			ftpDownloadTransfer->deleteLater();
		});

		ftpDownloadTransfer->progressPercent.valueChanged().connect([&ftpSingleton](const int &progressPercent) {
			ftpSingleton.set_progress_percent_download(progressPercent);
		});

		networkAccessManager.registerTransfer(*ftpDownloadTransfer);
		ftpSingleton.set_is_downloading(true);
	};
	ftpSingleton.on_request_ftp_download(startFtpDownload);

	auto startFtpUpload = [&]() {
		const auto &url = Url(ftpSingleton.get_url_upload().data());
		auto ftpUploadTransfer = new FtpUploadTransferHandle(ftpFile, url, true);

		ftpUploadTransfer->finished.connect([=, &ftpSingleton]() {
			spdlog::info("FtpUploadTransferHandle::finished() - uploaded {} bytes", ftpUploadTransfer->numberOfBytesTransferred.get());
			ftpSingleton.set_is_uploading(false);
			ftpUploadTransfer->deleteLater();
		});

		ftpUploadTransfer->progressPercent.valueChanged().connect([&ftpSingleton](const int &progressPercent) {
			ftpSingleton.set_progress_percent_upload(progressPercent);
		});

		networkAccessManager.registerTransfer(*ftpUploadTransfer);
		ftpSingleton.set_is_uploading(true);
	};
	ftpSingleton.on_request_ftp_upload(startFtpUpload);
#endif
}

void ApplicationEngine::InitMqttDemo(const MqttSingleton &mqttSingleton, IMqttClient &mqttClient)
{
#ifdef MOSQUITTO_AVAILABLE
	static std::shared_ptr<slint::VectorModel<slint::SharedString>> mqttSubscriptionsModel(new slint::VectorModel<slint::SharedString>);
	mqttSingleton.set_subscribed_topics(mqttSubscriptionsModel);

	static auto caFilePath = std::filesystem::current_path().append("mosquitto.org.crt").string();
	mqttSingleton.set_ca_file_path(slint::SharedString(caFilePath));

	mqttSingleton.set_last_will_topic("farewell");
	mqttSingleton.set_last_will_payload("boot(m_incarnations[++i]);");
	mqttSingleton.set_username("ro");
	mqttSingleton.set_password("readonly");
	mqttSingleton.set_url("test.mosquitto.org");
	mqttSingleton.set_port(1883);
	mqttSingleton.set_topic("my_test_topic");
	mqttSingleton.set_payload("DEADBEEF");

	auto mqttTopicValidator = [&]() {
		const auto topic = mqttSingleton.get_topic().data();
		const auto isValid = MqttLib::instance().isValidTopicNameForSubscription(topic);
		mqttSingleton.set_is_topic_valid(isValid);
	};
	mqttSingleton.on_user_edited_topic(mqttTopicValidator);
	mqttTopicValidator(); // initial validation of default topic set above

	auto translateConnectionState = [](MqttClient::ConnectionState connectionState) -> MqttConnectionState {
		switch (connectionState) {
		case MqttClient::ConnectionState::CONNECTING: return MqttConnectionState::Connecting;
		case MqttClient::ConnectionState::CONNECTED: return MqttConnectionState::Connected;
		case MqttClient::ConnectionState::DISCONNECTING: return MqttConnectionState::Disconnecting;
		case MqttClient::ConnectionState::DISCONNECTED: return MqttConnectionState::Disconnected;
		default:
			spdlog::error("Cannot translate IMqttClient::ConnectionState {} to MqttConnectionState!");
			return MqttConnectionState::Disconnected;
		}
	};

	auto translateSubscriptionState = [](MqttClient::SubscriptionState subscriptionState) -> MqttSubscriptionState {
		switch (subscriptionState) {
		case MqttClient::SubscriptionState::SUBSCRIBING: return MqttSubscriptionState::Subscribing;
		case MqttClient::SubscriptionState::SUBSCRIBED: return MqttSubscriptionState::Subscribed;
		case MqttClient::SubscriptionState::UNSUBSCRIBING: return MqttSubscriptionState::Unsubscribing;
		case MqttClient::SubscriptionState::UNSUBSCRIBED: return MqttSubscriptionState::Unsubscribed;
		default:
			spdlog::error("Cannot translate IMqttClient::SubscriptionState {} to MqttSubscriptionState!");
			return MqttSubscriptionState::Unsubscribed;
		}
	};

	auto onMqttConnectionStateChanged = [&](const MqttClient::ConnectionState &connectionState) {
		mqttSingleton.set_connection_state(translateConnectionState(connectionState));
	};
	mqttClient.connectionState.valueChanged().connect(onMqttConnectionStateChanged);

	auto onMqttSubscriptionStateChanged = [&](const MqttClient::SubscriptionState &subscriptionState) {
		mqttSingleton.set_subscription_state(translateSubscriptionState(subscriptionState));
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
		mqttSingleton.set_message(slint::SharedString(timestring.substr(0, timestring.size()-1) + " - " + topic + " - " + payload));
	};
	mqttClient.msgReceived.connect(onMqttMessageReceived);

	auto connectToMqttBroker = [&]() {
		const auto setLastWill = mqttSingleton.get_set_last_will_on_connect();
		const std::string lastWillTopic = mqttSingleton.get_last_will_topic().data();
		const std::string lastWillPayload = mqttSingleton.get_last_will_payload().data();

		const auto setTls = mqttSingleton.get_set_ca_file_path_on_connect();
		const std::string tlsCaFilePath = mqttSingleton.get_ca_file_path().data();

		const auto setUsernameAndPasswordOnConnect = mqttSingleton.get_set_username_and_password_on_connect();
		const std::string username = setUsernameAndPasswordOnConnect ? mqttSingleton.get_username().data() : std::string();
		const std::string password = setUsernameAndPasswordOnConnect ? mqttSingleton.get_password().data() : std::string();

		const auto &url = Url(mqttSingleton.get_url().data());
		const auto port = mqttSingleton.get_port();

		if (setLastWill)
			mqttClient.setWill(lastWillTopic, lastWillPayload.size(), &lastWillPayload);
		if (setTls)
			mqttClient.setTls(tlsCaFilePath);
		mqttClient.setUsernameAndPassword(username, password);
		mqttClient.connect(url, port, 10);
	};
	mqttSingleton.on_request_mqtt_connect(connectToMqttBroker);

	auto disconnectFromMqttBroker = [&]() {
		mqttClient.disconnect();
	};
	mqttSingleton.on_request_mqtt_disconnect(disconnectFromMqttBroker);

	auto subscribeToMqttTopic = [&]() {
		const auto topic = mqttSingleton.get_topic().data();
		mqttClient.subscribe(topic);
	};
	mqttSingleton.on_request_mqtt_subscribe(subscribeToMqttTopic);

	auto unsubscribeFromMqttTopic = [&](slint::SharedString topic) {
		mqttClient.unsubscribe(topic.data());
	};
	mqttSingleton.on_request_mqtt_unsubscribe(unsubscribeFromMqttTopic);

	auto publishMqttMessage = [&]() {
		const auto topic = mqttSingleton.get_topic().data();
		const std::string payload = mqttSingleton.get_payload().data();
		mqttClient.publish(NULL, topic, payload.length(), payload.c_str());
	};
	mqttSingleton.on_request_mqtt_publish(publishMqttMessage);
#endif
}
