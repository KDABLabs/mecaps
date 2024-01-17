#pragma once

#include "app_window.h"
#ifdef MOSQUITTO_AVAILABLE
#include "mqtt.h"
#endif
#ifdef CURL_AVAILABLE
#include "network_access_manager.h"
#endif
#include "slint.h"

class ApplicationEngine
{
	friend class ApplicationEngineUnitTestHarness;

  private:
	ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow);
	~ApplicationEngine();

	ApplicationEngine(const ApplicationEngine&) = delete;
	ApplicationEngine &operator=(const ApplicationEngine&) = delete;

  public:
	static ApplicationEngine &init(const slint::ComponentHandle<AppWindow> &appWindow);

  private:
	static void InitCounterDemo(const CounterSingleton &uiPageCounter);
#ifdef CURL_AVAILABLE
	static void InitHttpDemo(const HttpSingleton &httpSingleton, const INetworkAccessManager &networkAccessManager);
	static void InitFtpDemo(const FtpSingleton &ftpSingleton, const INetworkAccessManager &networkAccessManager);
#endif
#ifdef MOSQUITTO_AVAILABLE
	static void InitMqttDemo(const MqttSingleton &mqttSingleton, IMqttClient &mqttClient);
#endif
};
