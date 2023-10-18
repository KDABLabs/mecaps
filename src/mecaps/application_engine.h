#pragma once

#include "app_window.h"
#include "slint.h"

class ApplicationEngine
{
  public:
	static ApplicationEngine &init(const slint::ComponentHandle<AppWindow> &appWindow);

  private:
	ApplicationEngine(const slint::ComponentHandle<AppWindow> &appWindow);
	~ApplicationEngine();

	void InitCounterDemo(const CounterSingleton &uiPageCounter);
	void InitHttpDemo(const HttpSingleton &httpSingleton);
	void InitFtpDemo(const FtpSingleton &ftpSingleton);
};
