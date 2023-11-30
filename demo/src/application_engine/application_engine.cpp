#include "application_engine.h"
#ifdef CURL_AVAILABLE
#include "ftp_transfer_handle.h"
#include "http_transfer_handle.h"
#endif
#ifdef MOSQUITTO_AVAILABLE
#include "mqtt.h"
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

#ifndef MOSQUITTO_AVAILABLE
	appSingleton.set_mosquitto_available(false);
#endif

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
			delete httpTransfer; // TODO: use deferred deletion once it's 
								 // implemented in KDUtils
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
			delete ftpDownloadTransfer;
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
			delete ftpUploadTransfer;
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

void ApplicationEngine::InitMqttDemo(const MqttSingleton &mqttSingleton)
{
#ifdef MOSQUITTO_AVAILABLE
	MQTT::libInit();

	static auto &uiPageMqtt = mqttSingleton;
	static MQTT mqttClient = MQTT("mecapitto", true, true);

	static std::shared_ptr<slint::VectorModel<slint::SharedString>> mqttSubscriptionsModel(new slint::VectorModel<slint::SharedString>);
	uiPageMqtt.set_subscribed_topics(mqttSubscriptionsModel);

	static auto caFilePath = std::filesystem::current_path().append("mosquitto.org.crt").string();
	uiPageMqtt.set_ca_file_path(slint::SharedString(caFilePath));

	uiPageMqtt.set_last_will_topic("farewell");
	uiPageMqtt.set_last_will_payload("boot(m_incarnations[++i]);");
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
		const auto setLastWill = uiPageMqtt.get_set_last_will_on_connect();
		const std::string lastWillTopic = uiPageMqtt.get_last_will_topic().data();
		const std::string lastWillPayload = uiPageMqtt.get_last_will_payload().data();

		const auto setTls = uiPageMqtt.get_set_ca_file_path_on_connect();
		const std::string tlsCaFilePath = uiPageMqtt.get_ca_file_path().data();

		const auto setUsernameAndPasswordOnConnect = uiPageMqtt.get_set_username_and_password_on_connect();
		const std::string username = setUsernameAndPasswordOnConnect ? uiPageMqtt.get_username().data() : std::string();
		const std::string password = setUsernameAndPasswordOnConnect ? uiPageMqtt.get_password().data() : std::string();

		const auto &url = Url(uiPageMqtt.get_url().data());
		const auto port = uiPageMqtt.get_port();

		if (setLastWill)
			mqttClient.setWill(lastWillTopic, lastWillPayload.size(), &lastWillPayload);
		if (setTls)
			mqttClient.setTls(tlsCaFilePath);
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
#endif
}

void ApplicationEngine::DeinitMqttDemo()
{
#ifdef MOSQUITTO_AVAILABLE
	MQTT::libCleanup();
#endif
}
