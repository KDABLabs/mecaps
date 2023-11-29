#pragma once

#include "app_window.h"
#include "network_access_manager.h"
#include "slint.h"

class ApplicationEngine
{
	friend class ApplicationEngineUnitTestHarness;

  public:
	static ApplicationEngine &init(const slint::ComponentHandle<AppWindow> &appWindow);

  private:
	ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow);
	~ApplicationEngine();

	static void InitCounterDemo(const CounterSingleton &uiPageCounter);
	static void InitHttpDemo(const HttpSingleton &httpSingleton, const INetworkAccessManager &networkAccessManager);
	static void InitFtpDemo(const FtpSingleton &ftpSingleton, const INetworkAccessManager &networkAccessManager);
	static void InitMqttDemo(const MqttSingleton &mqttSingleton);
	static void DeinitMqttDemo();
};
