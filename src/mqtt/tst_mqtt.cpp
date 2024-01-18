#include "KDFoundation/core_application.h"
#include "mqtt.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <signal_spy.h>
#include "fakeit.h" // <= include after doctest.h

using namespace fakeit;

class MqttUnitTestHarness
{
  public:
	MqttUnitTestHarness()
	{
		MqttLib::instance().m_isInitialized = false;
		MqttLib::instance().m_mosquittoLib = &m_libMock.get();

		// Setup default return values for faked methods
		When(Method(libMock(), init)).AlwaysReturn(MOSQ_ERR_SUCCESS);
		When(Method(libMock(), cleanup)).AlwaysReturn(MOSQ_ERR_SUCCESS);
		When(Method(libMock(), version)).AlwaysReturnAndSet(MOSQ_ERR_SUCCESS, 0, 0, 0);

		// Have string conversion methods not be faked/mocked and call actual implementations
		When(Method(libMock(), connackString)).AlwaysDo([](auto connack_code) { return MosquittoLib::instance().connackString(connack_code); } );
		When(Method(libMock(), errorString)).AlwaysDo([](auto mosq_errno) { return MosquittoLib::instance().errorString(mosq_errno); } );
		When(Method(libMock(), reasonString)).AlwaysDo([](auto reason_code) { return MosquittoLib::instance().reasonString(reason_code); } );
	}

	void injectMockIntoClient(MqttClient &mqttClient)
	{
		// Inject mocked MosquittoClient into MqttClient
		mqttClient.m_mosquitto.init(&m_clientMock.get(), &mqttClient);
	}

	static MqttClient::EventLoopHook &eventLoopHook(MqttClient &mqttClient)
	{
		return mqttClient.m_eventLoopHook;
	}

	static void onConnected(MqttClient &mqttClient, int connackCode)
	{
		mqttClient.onConnected(connackCode);
	}

	static void onDisconnected(MqttClient &mqttClient, int reasonCode)
	{
		mqttClient.onDisconnected(reasonCode);
	}

	static void onPublished(MqttClient &mqttClient, int msgId)
	{
		mqttClient.onPublished(msgId);
	}

	static void onMessage(MqttClient &mqttClient, const mosquitto_message *message)
	{
		mqttClient.onMessage(message);
	}

	static void onSubscribed(MqttClient &mqttClient, int msgId, int qosCount, const int *grantedQos)
	{
		mqttClient.onSubscribed(msgId, qosCount, grantedQos);
	}

	static void onUnsubscribed(MqttClient &mqttClient, int msgId)
	{
		mqttClient.onUnsubscribed(msgId);
	}

	static void onError(MqttClient &mqttClient)
	{
		mqttClient.onError();
	}

	static void onReadOpRequested(MqttClient &mqttClient)
	{
		mqttClient.onReadOpRequested();
	}

	static void onWriteOpRequested(MqttClient &mqttClient)
	{
		mqttClient.onWriteOpRequested();
	}

	static void onMiscTaskRequested(MqttClient &mqttClient)
	{
		mqttClient.onMiscTaskRequested();
	}

	Mock<MosquittoLib> &libMock() { return m_libMock; }
	Mock<MosquittoClient> &clientMock() { return m_clientMock; }

  private:
	Mock<MosquittoLib> m_libMock;
	Mock<MosquittoClient> m_clientMock;
};

auto app = CoreApplication();

TEST_SUITE("MQTT")
{
	TEST_CASE("MqttLib::init()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;

		SUBCASE("triggers initialization on first call")
		{
			// GIVEN
			const auto result = MqttLib::instance().init();

			// THEN
			REQUIRE(result == MOSQ_ERR_SUCCESS);
			Verify(Method(mqttUnitTestHarness.libMock(), init)).Once();
		}

		SUBCASE("does NOT trigger initialization on consecutive calls")
		{
			// GIVEN
			MqttLib::instance().init();
			Verify(Method(mqttUnitTestHarness.libMock(), init)).Once();

			// WHEN
			const auto result = MqttLib::instance().init();

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
			Verify(Method(mqttUnitTestHarness.libMock(), init)).Once();
		}
	}

	TEST_CASE("MqttLib::cleanup()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;

		SUBCASE("triggers cleanup")
		{
			// GIVEN
			const auto result = MqttLib::instance().cleanup();

			// THEN
			REQUIRE(result == MOSQ_ERR_SUCCESS);
			Verify(Method(mqttUnitTestHarness.libMock(), cleanup)).Once();
		}
	}

	TEST_CASE("CTOR member initialization")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();

		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("initial connection state is DISCONNECTED")
		{
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::DISCONNECTED);
		}

		SUBCASE("initial subscription state is UNSUBSCRIBED")
		{
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::UNSUBSCRIBED);
		}

		SUBCASE("initial subscriptions list is empty")
		{
			REQUIRE(mqttClient.subscriptions.get().empty());
		}
	}

	TEST_CASE("MqttClient::setTls()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), tlsSet));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		File invalidCaFile = File("this_file_does_not_exist.crt");

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			const auto result = mqttClient.setTls(invalidCaFile);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.setTls(invalidCaFile);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			const auto result = mqttClient.setTls(invalidCaFile);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if file does not exist")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.setTls(invalidCaFile);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::tlsSet once with correct arguments")
		{
			// GIVEN
			auto dummyCaFile = File(std::filesystem::current_path().append("dummy.org.crt").string());
			dummyCaFile.open(std::ios::out);
			dummyCaFile.close();
			REQUIRE(dummyCaFile.exists());

			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.setTls(dummyCaFile);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), tlsSet)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), tlsSet).Using(dummyCaFile.path().c_str(), nullptr, nullptr, nullptr, Any()));
		}
	}

	TEST_CASE("MqttClient::setUsernameAndPassword()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), usernamePasswordSet));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const std::string &user = "username";
		const std::string &password = "password";

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			const auto result = mqttClient.setUsernameAndPassword(user, password);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.setUsernameAndPassword(user, password);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			const auto result = mqttClient.setUsernameAndPassword(user, password);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::usernamePasswordSet once with correct arguments")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.setUsernameAndPassword(user, password);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), usernamePasswordSet)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), usernamePasswordSet).Using(user.c_str(), password.c_str()));
		}
	}

	TEST_CASE("MqttClient::setWill()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), willSet));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const std::string topic = "last_will";
		const auto payload = std::string("payload");
		const auto qos = 0;
		const auto retain = false;

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			const auto result = mqttClient.setWill(topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.setWill(topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			const auto result = mqttClient.setWill(topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::willSet once with correct arguments")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.setWill(topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), willSet)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), willSet).Using(topic.c_str(), payload.length(), payload.c_str(), qos, retain));
		}
	}

	TEST_CASE("MqttClient::connect()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), connect));
		Fake(Method(mqttUnitTestHarness.clientMock(), socket));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const auto host = Url("test.org");
		const auto port = 0;
		const auto keepalive = 60;

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is CONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("sets connection state to CONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::CONNECTING);
		}

		SUBCASE("calls MosquittoClient::connect once with correct arguments")
		{
			// GIVEN
			std::string hostUrl;
			When(Method(mqttUnitTestHarness.clientMock(), connect)).AlwaysDo([&hostUrl](const char *host, int port, int keepalive) {
				// Manually capture argument: host
				// I ran into issues when validating host the default way
				// maybe related to https://github.com/eranpeer/FakeIt/issues/31
				hostUrl = host;
				return MOSQ_ERR_SUCCESS;
			});
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), connect)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), connect).Using(Any(), port, keepalive));
			REQUIRE(hostUrl == host.url());
		}

		SUBCASE("engages event loop hook if MosquittoClient::connect returns SUCCESS")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), connect)).Return(MOSQ_ERR_SUCCESS);
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			REQUIRE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}

		SUBCASE("does NOT engage event loop hook if MosquittoClient::connect returns ERROR")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), connect)).Return(MOSQ_ERR_UNKNOWN);
			const auto result = mqttClient.connect(host, port, keepalive);

			// THEN
			REQUIRE_FALSE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}
	}

	TEST_CASE("MqttClient::disconnect()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), disconnect));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			const auto result = mqttClient.disconnect();

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.disconnect();

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("sets connection state to DISCONNECTING")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.disconnect();

			// THEN
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::DISCONNECTING);
		}

		SUBCASE("calls MosquittoClient::disconnect once")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.disconnect();

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), disconnect)).Once();
		}
	}

	TEST_CASE("MqttClient::publish()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), publish));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		int msgId = 0;
		const auto *topic = "test";
		const auto payload = std::string("payload");
		const auto qos = 0;
		const auto retain = false;

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.publish(&msgId, topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::publish once with correct arguments")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.publish(&msgId, topic, payload.length(), payload.c_str(), qos, retain);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), publish)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), publish).Using(&msgId, topic, payload.length(), payload.c_str(), qos, retain));
		}
	}

	TEST_CASE("MqttClient::subscribe()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), subscribe));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const auto *pattern = "pattern";
		const auto qos = 0;

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.subscribe(pattern, qos);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::subscribe once with correct arguments")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.subscribe(pattern, qos);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), subscribe)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), subscribe).Using(Any(), pattern, qos));
		}

		SUBCASE("sets subscription state to SUBSCRIBING if MosquittoClient::subscribe returns SUCCESS")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), subscribe)).Return(MOSQ_ERR_SUCCESS);
			const auto result = mqttClient.subscribe(pattern, qos);

			// THEN
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::SUBSCRIBING);
		}

		SUBCASE("does NOT set subscription state to SUBSCRIBING if MosquittoClient::subscribe returns ERROR")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), subscribe)).Return(MOSQ_ERR_UNKNOWN);
			const auto result = mqttClient.subscribe(pattern, qos);

			// THEN
			REQUIRE_FALSE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::SUBSCRIBING);
		}
	}

	TEST_CASE("MqttClient::unsubscribe()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), unsubscribe));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const auto *pattern = "pattern";

		SUBCASE("returns MOSQ_ERR_UNKNOWN if connection state is DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTED);

			// WHEN
			const auto result = mqttClient.unsubscribe(pattern);

			// THEN
			REQUIRE(result == MOSQ_ERR_UNKNOWN);
		}

		SUBCASE("calls MosquittoClient::unsubscribe once with correct arguments")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			const auto result = mqttClient.unsubscribe(pattern);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), unsubscribe)).Once();
			Verify(Method(mqttUnitTestHarness.clientMock(), unsubscribe).Using(Any(), pattern));
		}

		SUBCASE("sets subscription state to UNSUBSCRIBING if MosquittoClient::unsubscribe returns SUCCESS")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), unsubscribe)).Return(MOSQ_ERR_SUCCESS);
			const auto result = mqttClient.unsubscribe(pattern);

			// THEN
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::UNSUBSCRIBING);
		}

		SUBCASE("does NOT set subscription state to UNSUBSCRIBING if MosquittoClient::unsubscribe returns ERROR")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);

			// WHEN
			When(Method(mqttUnitTestHarness.clientMock(), unsubscribe)).Return(MOSQ_ERR_UNKNOWN);
			const auto result = mqttClient.unsubscribe(pattern);

			// THEN
			REQUIRE_FALSE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::UNSUBSCRIBING);
		}
	}

	TEST_CASE("MqttClient::onConnected()")
	{
		auto mqttUnitTestHarness = MqttUnitTestHarness();
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("sets connection state to CONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			MqttUnitTestHarness::onConnected(mqttClient, MOSQ_ERR_SUCCESS);

			// THEN
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::CONNECTED);
		}

		SUBCASE("sets connection state to DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTING);

			// WHEN
			MqttUnitTestHarness::onConnected(mqttClient, MOSQ_ERR_CONN_REFUSED);

			// THEN
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::DISCONNECTED);
		}
	}

	TEST_CASE("MqttClient::onDisconnected()")
	{
		auto mqttUnitTestHarness = MqttUnitTestHarness();
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("disengages event loop hook")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			MqttUnitTestHarness::onDisconnected(mqttClient, 0);

			// THEN
			REQUIRE_FALSE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}

		SUBCASE("sets connection state to DISCONNECTED")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::DISCONNECTING);

			// WHEN
			MqttUnitTestHarness::onDisconnected(mqttClient, 0);

			// THEN
			REQUIRE(mqttClient.connectionState.get() == MqttClient::ConnectionState::DISCONNECTED);
		}
	}

	TEST_CASE("MqttClient::onPublished")
	{
		auto mqttUnitTestHarness = MqttUnitTestHarness();
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("emits signal msgPublished")
		{
			// GIVEN
			SignalSpy spy(mqttClient.msgPublished);

			// WHEN
			MqttUnitTestHarness::onPublished(mqttClient, 0);
			
			// THEN
			REQUIRE(spy.count() == 1);
		}
	}

	TEST_CASE("MqttClient::onMessage")
	{
		auto mqttUnitTestHarness = MqttUnitTestHarness();
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("emits signal msgReceived")
		{
			// GIVEN
			SignalSpy spy(mqttClient.msgReceived);

			// WHEN
			MqttUnitTestHarness::onMessage(mqttClient, NULL);
			
			// THEN
			REQUIRE(spy.count() == 1);
		}
	}

	TEST_CASE("MqttClient::onSubscribed()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const int grantedQos[] = { 0 };
		const int msgId = 1;
		const auto *topic = "testTopic";
		
		When(Method(mqttUnitTestHarness.clientMock(), subscribe)).AlwaysReturnAndSet(MOSQ_ERR_SUCCESS, msgId);

		SUBCASE("sets SUBSCRIPTION state to SUBSCRIBED if a subscription operation is pending for topic")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);
			mqttClient.subscribe(topic);

			// WHEN
			MqttUnitTestHarness::onSubscribed(mqttClient, msgId, 1, grantedQos);

			// THEN
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::SUBSCRIBED);
		}

		SUBCASE("adds topic to list of subscribed topics if a subscription operation is pending for topic")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);
			mqttClient.subscribe(topic);

			// WHEN
			MqttUnitTestHarness::onSubscribed(mqttClient, msgId, 1, grantedQos);

			// THEN
			REQUIRE(mqttClient.subscriptions.get().at(0) == topic);
		}
	}

	TEST_CASE("MqttClient::onUnsubscribed()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		const int grantedQos[] = { 0 };
		const int msgId = 1;
		const auto *topic = "testTopic";
		
		When(Method(mqttUnitTestHarness.clientMock(), subscribe)).AlwaysReturnAndSet(MOSQ_ERR_SUCCESS, msgId);
		When(Method(mqttUnitTestHarness.clientMock(), unsubscribe)).AlwaysReturnAndSet(MOSQ_ERR_SUCCESS, msgId);

		SUBCASE("sets SUBSCRIPTION state to UNSUBSCRIBED if a subscription operation is pending for topic")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);
			mqttClient.subscribe(topic);
			MqttUnitTestHarness::onSubscribed(mqttClient, msgId, 1, grantedQos);
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::SUBSCRIBED);
			mqttClient.unsubscribe(topic);

			// WHEN
			MqttUnitTestHarness::onUnsubscribed(mqttClient, msgId);

			// THEN
			REQUIRE(mqttClient.subscriptionState.get() == MqttClient::SubscriptionState::UNSUBSCRIBED);
		}

		SUBCASE("adds topic to list of subscribed topics if a subscription operation is pending for topic")
		{
			// GIVEN
			mqttClient.connectionState.set(MqttClient::ConnectionState::CONNECTED);
			mqttClient.subscribe(topic);
			MqttUnitTestHarness::onSubscribed(mqttClient, msgId, 1, grantedQos);
			REQUIRE(mqttClient.subscriptions.get().at(0) == topic);
			mqttClient.unsubscribe(topic);

			// WHEN
			MqttUnitTestHarness::onUnsubscribed(mqttClient, msgId);

			// THEN
			REQUIRE(mqttClient.subscriptions.get().empty());
		}
	}

	TEST_CASE("MqttClient::onError()")
	{
		auto mqttUnitTestHarness = MqttUnitTestHarness();
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		SUBCASE("emits signal error")
		{
			// GIVEN
			SignalSpy spy(mqttClient.error);

			// WHEN
			MqttUnitTestHarness::onError(mqttClient);
			
			// THEN
			REQUIRE(spy.count() == 1);
		}
	}

	TEST_CASE("MqttClient::onReadOpRequested()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), loopRead));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		SUBCASE("calls MosquittoClient::loopRead once")
		{
			// GIVEN
			MqttUnitTestHarness::onReadOpRequested(mqttClient);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), loopRead)).Once();
		}
	}

	TEST_CASE("MqttClient::onWriteOpRequested()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), wantWrite));
		Fake(Method(mqttUnitTestHarness.clientMock(), loopWrite));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		SUBCASE("calls MosquittoClient::wantWrite once")
		{
			// GIVEN
			MqttUnitTestHarness::onWriteOpRequested(mqttClient);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), wantWrite)).Once();
		}

		SUBCASE("calls MosquittoClient::loopWrite once if MosquittoClient::wantWrite returns true")
		{
			// GIVEN
			When(Method(mqttUnitTestHarness.clientMock(), wantWrite)).Return(true);

			// WHEN
			MqttUnitTestHarness::onWriteOpRequested(mqttClient);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), loopWrite)).Once();
		}

		SUBCASE("calls MosquittoClient::loopWrite once if MosquittoClient::wantWrite returns false")
		{
			// GIVEN
			When(Method(mqttUnitTestHarness.clientMock(), wantWrite)).Return(false);

			// WHEN
			MqttUnitTestHarness::onWriteOpRequested(mqttClient);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), loopWrite)).Never();
		}
	}

	TEST_CASE("MqttClient::onMiscTaskRequested()")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), loopMisc));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		SUBCASE("calls MosquittoClient::loopMisc once")
		{
			// GIVEN
			MqttUnitTestHarness::onMiscTaskRequested(mqttClient);

			// THEN
			Verify(Method(mqttUnitTestHarness.clientMock(), loopMisc)).Once();
		}
	}

	TEST_CASE("MqttClient::EventLoopHook")
	{
		MqttUnitTestHarness mqttUnitTestHarness;
		MqttLib::instance().init();
		auto mqttClient = MqttClient("tst_mqtt");

		Fake(Method(mqttUnitTestHarness.clientMock(), wantWrite));
		Fake(Method(mqttUnitTestHarness.clientMock(), loopWrite));
		mqttUnitTestHarness.injectMockIntoClient(mqttClient);

		SUBCASE("MqttClient::EventLoopHook::engage() engages hook if socket >= 0")
		{
			// GIVEN
			auto socket = 0;

			// WHEN
			MqttUnitTestHarness::eventLoopHook(mqttClient).engage(socket);

			// THEN
			REQUIRE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}

		SUBCASE("MqttClient::EventLoopHook::engage() does not engage hook if socket < 0")
		{
			// GIVEN
			auto socket = -1;

			// WHEN
			MqttUnitTestHarness::eventLoopHook(mqttClient).engage(socket);

			// THEN
			REQUIRE_FALSE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}

		SUBCASE("MqttClient::EventLoopHook::disengage() disengages engaged hook")
		{
			// GIVEN
			auto socket = 0;
			MqttUnitTestHarness::eventLoopHook(mqttClient).engage(socket);
			REQUIRE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());

			// WHEN
			MqttUnitTestHarness::eventLoopHook(mqttClient).disengage();

			// THEN
			REQUIRE_FALSE(MqttUnitTestHarness::eventLoopHook(mqttClient).isEngaged());
		}
	}
}
