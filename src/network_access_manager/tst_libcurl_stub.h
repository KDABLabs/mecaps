#pragma once

#include <curl/curl.h>
#include <functional>

#define FFF_ARG_HISTORY_LEN 15

/* Configure FFF to use std::function, which enables capturing lambdas */
#define CUSTOM_FFF_FUNCTION_TEMPLATE(RETURN, FUNCNAME, ...) \
	std::function<RETURN (__VA_ARGS__)> FUNCNAME

#include "fff.h"

DEFINE_FFF_GLOBALS;

// List of fakes used by this unit test
#define FFF_FAKES_LIST(FAKE) \
FAKE(curl_global_init) \
	FAKE(curl_easy_init) \
	FAKE(curl_easy_cleanup) \
	FAKE(curl_easy_setopt) \
	FAKE(curl_easy_getinfo) \
	FAKE(curl_multi_init) \
	FAKE(curl_multi_setopt) \
	FAKE(curl_multi_add_handle) \
	FAKE(curl_multi_remove_handle) \
	FAKE(curl_multi_socket_action) \
	FAKE(curl_multi_info_read)

// Declare and define curl_global fakes
FAKE_VALUE_FUNC(CURLcode, curl_global_init, long);

// Declare and define curl_easy fakes
FAKE_VALUE_FUNC(CURL*, curl_easy_init);
FAKE_VOID_FUNC(curl_easy_cleanup, CURL*);
FAKE_VALUE_FUNC_VARARG(CURLcode, curl_easy_setopt, CURL*, CURLoption, ...);
FAKE_VALUE_FUNC_VARARG(CURLcode, curl_easy_getinfo, CURL*, CURLINFO, ...);

// Declare and define curl_multi fakes
FAKE_VALUE_FUNC(CURLM*, curl_multi_init);
FAKE_VALUE_FUNC_VARARG(CURLMcode, curl_multi_setopt, CURLM*, CURLMoption, ...);
FAKE_VALUE_FUNC(CURLMcode, curl_multi_add_handle, CURLM*, CURL*);
FAKE_VALUE_FUNC(CURLMcode, curl_multi_remove_handle, CURLM*, CURL*);
FAKE_VALUE_FUNC(CURLMcode, curl_multi_socket_action, CURLM*, curl_socket_t, int, int*);
FAKE_VALUE_FUNC(CURLMsg*, curl_multi_info_read, CURLM*,int*);

// Provide function to reset fakes
// and common FFF internal structures
void fff_setup()
{
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}
