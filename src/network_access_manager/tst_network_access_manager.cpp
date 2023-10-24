/*
 * This implements white box tests for class NetworkAccessManager
 * and the related transfer classes deriving from AbstractTransferHandle.
 *
 * In order to test libcurl calls, i.e.
 * - which libcurl functions are called
 * - with which parameters is a libcurl function called
 * - in which sequence do these calls occurr
 * ... and not the actual libcurl implementation, a stub is used to
 * substitute libcurl. This stub uses the Fake Function Framework
 * and is implemented in tst_libcurl_stub.h
 *
 * Additionally to the libcurl stub we use NetworkAccessManagerUnitTestHarness
 * to be able to
 * - invoke callbacks, that would otherwise be invoked by libcurl
 * - access a few private members of NetworkAccessManager to do
 *   white box testing
 *
 */

#include <KDFoundation/core_application.h>
#include "ftp_transfer_handle.h"
#include "http_transfer_handle.h"
#include "network_access_manager.h"
#include "tst_libcurl_stub.h"

#include <cstdarg>
#include <unordered_map>
#include <variant>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

class GenericTransferHandleUnitTest : public AbstractTransferHandle
{
  public:
	GenericTransferHandleUnitTest(const Url &url, bool verbose = false) : AbstractTransferHandle(url, verbose) { }

  protected:
	virtual void transferDoneCallbackImpl(CURLcode reault) override { }
};

class AbstractTransferHandleUnitTestHarness
{
  public:
	static void *errorBuffer(AbstractTransferHandle &transfer) { return &transfer.m_errorBuffer; }
	static void *readCallback() { return (void*)(&AbstractTransferHandle::readCallback); }
	static void *writeCallback() { return (void*)(&AbstractTransferHandle::writeCallback); }
};

class GenericFtpTransferHandleUnitTest : public AbstractFtpTransferHandle
{
  public:
	GenericFtpTransferHandleUnitTest(const Url &url, bool verbose = false) : AbstractFtpTransferHandle(url, verbose) { }

  protected:
	virtual int progressCallbackImpl(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) override { return -1; }
	virtual void transferDoneCallbackImpl(CURLcode reault) override { }
};

class AbstractFtpTransferHandleUnitTestHarness
{
  public:
	static void *progressCallback() { return (void*)(&AbstractFtpTransferHandle::progressCallback); }
	static void *file(AbstractFtpTransferHandle &transfer) { return transfer.m_file; }
};

class NetworkAccessManagerUnitTestHarness
{
  public:
	static int socketCallback(CURL *handle, curl_socket_t socket, int eventType) {
		return NetworkAccessManager::socketCallback(handle, socket, eventType, &NetworkAccessManager::instance(), nullptr);
	}
	static int timerCallback(CURLM *handle, long timeoutMs) {
		return NetworkAccessManager::timerCallback(handle, timeoutMs, &NetworkAccessManager::instance().m_timeoutTimer);
	}

	const Timer &timeoutTimer() { return NetworkAccessManager::instance().m_timeoutTimer; }
	NetworkAccessManager::FileDescriptorNotifierRegistry &fileDescriptorNotifierRegistry() { return NetworkAccessManager::instance().m_fdnRegistry; }
};


struct CurlDummyHandle {
	// we use this as 'non-nullptr' return value for
	// curl_multi_init() and curl_easy_init()
};

CurlDummyHandle curlDummyMultiHandle;
void *dummyMultiHandlePtr = &curlDummyMultiHandle;


TEST_SUITE("NetworkAccessManager::instance()")
{
	// NOTE: Due to NetworkAccessManager being a singleton, instantiation
	// and initialization of NetworkAccessManager is only done on first call
	// of NetworkAccessManager::instance().
	// We maintain all initialization related tests in this individual TEST_SUITE
	// so that the lifetime of NetworkAccessManager has a scope matching the
	// initialization related tests. Thus we are _independent of_ and do not
	// _interfere with_ other tests in this file.
	// Additionally we maintain all initialization related tests in one single(!)
	// TEST_CASE to ensure all initialization test related function calls and
	// assertions of this test are done in the correct order.
	// If we would split the initialization related tests into multipe TEST_CASEs
	// and / or SUBCASES, we would have to ensure all tests can be run independent
	// of each other generating valid results. This would be tricky because
	// NetworkAccessManager is a singleton.

	TEST_CASE("NetworkAccessManager::instance() triggers initialization on first call only")
	{
		fff_setup();

		// NetworkAccessManager::instance() triggers initialization on first call
		{
			// GIVEN
			curl_multi_init_fake.return_val = dummyMultiHandlePtr;
			NetworkAccessManager::instance();
			NetworkAccessManagerUnitTestHarness unitTestHarness;

			// THEN (libcurl)
			REQUIRE(curl_global_init_fake.call_count == 1);
			REQUIRE(curl_global_init_fake.arg0_val == CURL_GLOBAL_ALL);

			REQUIRE(curl_multi_init_fake.call_count == 1);
			REQUIRE(curl_multi_init_fake.return_val);

			REQUIRE(curl_multi_setopt_fake.call_count == 4);

			REQUIRE(curl_multi_setopt_fake.arg0_history[0] == dummyMultiHandlePtr);
			REQUIRE(curl_multi_setopt_fake.arg1_history[0] == CURLMOPT_SOCKETFUNCTION);
			REQUIRE(curl_multi_setopt_fake.arg0_history[1] == dummyMultiHandlePtr);
			REQUIRE(curl_multi_setopt_fake.arg1_history[1] == CURLMOPT_SOCKETDATA);
			REQUIRE(curl_multi_setopt_fake.arg0_history[2] == dummyMultiHandlePtr);
			REQUIRE(curl_multi_setopt_fake.arg1_history[2] == CURLMOPT_TIMERFUNCTION);
			REQUIRE(curl_multi_setopt_fake.arg0_history[3] == dummyMultiHandlePtr);
			REQUIRE(curl_multi_setopt_fake.arg1_history[3] == CURLMOPT_TIMERDATA);

			// THEN (members)
			REQUIRE_FALSE(unitTestHarness.timeoutTimer().running.get());
		}

		// NetworkAccessManager::instance() does NOT trigger initialization on consecutive calls
		{
			// GIVEN
			NetworkAccessManager::instance();

			// THEN (call_count did not increase and still equals 1)
			REQUIRE(curl_global_init_fake.call_count == 1);
		}
	}
}


TEST_SUITE("NetworkAccessManager and TransferHandles")
{
	auto app = CoreApplication();

	TEST_CASE("AbstractTransferHandle")
	{
		fff_setup();
		CurlDummyHandle curlDummyEasyHandle;
		void *dummyEasyHandlePtr = &curlDummyEasyHandle;
		const auto url = Url("www.example.com");

		// curl_easy_setopt has three function parameters
		// the first two are handle via fff's arg(i)_history.
		// the third parameter is a va_list and requires the
		// following additional code to check argument values.
		std::unordered_map<CURLoption,std::variant<void*, long, std::string>> curl_easy_setopt_fake_arg3_history;
		curl_easy_setopt_fake.custom_fake = [&](CURL*, CURLoption option, va_list param) -> CURLcode {
			switch (option) {
			case CURLOPT_URL:
				curl_easy_setopt_fake_arg3_history[option] = std::string(va_arg(param,char*));
				break;
			case CURLOPT_VERBOSE:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,long);
				break;
			default:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,void*);
				break;
			}
			return CURLE_OK;
		};

		SUBCASE("AbstractTransferHandle CTOR calls curl_easy_init()")
		{
			// GIVEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			REQUIRE(curl_easy_init_fake.call_count == 1);
		}

		SUBCASE("AbstractTransferHandle CTOR aborts initialization on invalid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = nullptr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			REQUIRE(curl_easy_setopt_fake.call_count == 0);
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_URL on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_URL);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			std::string_view arg3_url = std::get<std::string>(curl_easy_setopt_fake_arg3_history[CURLOPT_URL]);
			REQUIRE(arg3_url == url.url());
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_VERBOSE on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url, false);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_VERBOSE);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto arg3_verbose = std::get<long>(curl_easy_setopt_fake_arg3_history[CURLOPT_VERBOSE]);
			REQUIRE(arg3_verbose == 0L);
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_PRIVATE on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_PRIVATE);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto *arg3_private = static_cast<AbstractTransferHandle*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_PRIVATE]));
			REQUIRE(arg3_private == &transfer);
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_ERRORBUFFER on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_ERRORBUFFER);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_buffer = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_ERRORBUFFER]);
			REQUIRE(arg3_buffer == AbstractTransferHandleUnitTestHarness::errorBuffer(transfer));
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_READFUNCTION on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_READFUNCTION);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_rFunc = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_READFUNCTION]);
			REQUIRE(arg3_rFunc == AbstractTransferHandleUnitTestHarness::readCallback());
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_READDATA on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_READDATA);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto arg3_rData = static_cast<AbstractTransferHandle*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_READDATA]));
			REQUIRE(arg3_rData == &transfer);
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_WRITEFUNCTION on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_WRITEFUNCTION);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_wFunc = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_WRITEFUNCTION]);
			REQUIRE(arg3_wFunc == AbstractTransferHandleUnitTestHarness::writeCallback());
		}

		SUBCASE("AbstractTransferHandle CTOR initializes CURLOPT_WRITEDATA on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_WRITEDATA);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const AbstractTransferHandle *arg3_wData = static_cast<AbstractTransferHandle*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_WRITEDATA]));
			REQUIRE(arg3_wData == &transfer);
		}

		SUBCASE("AbstractTransferHandle DTOR calls curl_easy_cleanup()")
		{
			// GIVEN
			auto *transfer = new GenericTransferHandleUnitTest(url);

			// WHEN
			delete transfer;

			// THEN
			REQUIRE(curl_easy_cleanup_fake.call_count == 1);
		}

		SUBCASE("AbstractTransferHandle::fromCurlEasyHandle() returns CURLINFO_PRIVATE")
		{
			// GIVEN
			auto transfer = GenericTransferHandleUnitTest(url);

			// WHEN
			transfer.fromCurlEasyHandle(dummyEasyHandlePtr);

			// THEN
			REQUIRE(curl_easy_getinfo_fake.call_count == 1);
			REQUIRE(curl_easy_getinfo_fake.arg0_history[0] == dummyEasyHandlePtr);
			REQUIRE(curl_easy_getinfo_fake.arg1_history[0] == CURLINFO_PRIVATE);
		}
	}

	TEST_CASE("HttpTransferHandle")
	{
		fff_setup();
		CurlDummyHandle dummyEasyHandle;
		void *dummyEasyHandlePtr = &dummyEasyHandle;
		const auto url = Url("www.example.com");

		// curl_easy_setopt has three function parameters
		// the first two are handle via fff's arg(i)_history.
		// the third parameter is a va_list and requires the
		// following additional code to check argument values.
		std::unordered_map<CURLoption,std::variant<void*, long, std::string>> curl_easy_setopt_fake_arg3_history;
		curl_easy_setopt_fake.custom_fake = [&](CURL*, CURLoption option, va_list param) -> CURLcode {
			switch (option) {
			case CURLOPT_NOPROGRESS:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,long);
				break;
			default:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,void*);
				break;
			}
			return CURLE_OK;
		};

		SUBCASE("HttpTransferHandle CTOR initializes CURLOPT_NOPROGRESS on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = HttpTransferHandle(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_NOPROGRESS);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto arg3_noProgress = std::get<long>(curl_easy_setopt_fake_arg3_history[CURLOPT_NOPROGRESS]);
			REQUIRE(arg3_noProgress == 1L);
		}
	}

	TEST_CASE("FtpTransferHandles")
	{
		fff_setup();
		CurlDummyHandle dummyEasyHandle;
		void *dummyEasyHandlePtr = &dummyEasyHandle;
		const auto url = Url("www.example.com");
		const auto testFilePath = std::filesystem::temp_directory_path().append("testFile.txt").string();
		auto testFile = File(testFilePath);

		// curl_easy_setopt has three function parameters
		// the first two are handle via fff's arg(i)_history.
		// the third parameter is a va_list and requires the
		// following additional code to check argument values.
		std::unordered_map<CURLoption,std::variant<std::monostate, void*, long, std::string>> curl_easy_setopt_fake_arg3_history;
		curl_easy_setopt_fake.custom_fake = [&](CURL*, CURLoption option, va_list param) -> CURLcode {
			switch (option) {
			case CURLOPT_NOPROGRESS:
			case CURLOPT_INFILESIZE_LARGE:
			case CURLOPT_UPLOAD:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,long);
				break;
			default:
				curl_easy_setopt_fake_arg3_history[option] = va_arg(param,void*);
				break;
			}
			return CURLE_OK;
		};

		SUBCASE("AbstractFtpTransferHandle CTOR initializes CURLOPT_NOPROGRESS on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericFtpTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_NOPROGRESS);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto arg3_noProgress = std::get<long>(curl_easy_setopt_fake_arg3_history[CURLOPT_NOPROGRESS]);
			REQUIRE(arg3_noProgress == 0L);
		}

		SUBCASE("AbstractFtpTransferHandle CTOR initializes CURLOPT_XFERINFOFUNCTION on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericFtpTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_XFERINFOFUNCTION);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_xferFunc = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_XFERINFOFUNCTION]);
			REQUIRE(arg3_xferFunc == AbstractFtpTransferHandleUnitTestHarness::progressCallback());
		}

		SUBCASE("AbstractFtpTransferHandle CTOR initializes CURLOPT_XFERINFODATA on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = GenericFtpTransferHandleUnitTest(url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_XFERINFODATA);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const AbstractFtpTransferHandle *arg3_xferData = static_cast<AbstractFtpTransferHandle*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_XFERINFODATA]));
			REQUIRE(arg3_xferData == &transfer);
		}

		SUBCASE("FtpDownloadTransferHandle CTOR clears CURLOPT_WRITEFUNCTION on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpDownloadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_WRITEFUNCTION);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_wFunc = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_WRITEFUNCTION]);
			REQUIRE(arg3_wFunc == NULL);
		}

		SUBCASE("FtpDownloadTransferHandle CTOR initializes CURLOPT_WRITEDATA on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpDownloadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_WRITEDATA);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const FILE *arg3_wData = static_cast<FILE*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_WRITEDATA]));
			REQUIRE(arg3_wData == AbstractFtpTransferHandleUnitTestHarness::file(transfer));
		}

		SUBCASE("FtpUploadTransferHandle CTOR initializes CURLOPT_UPLOAD on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_UPLOAD);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const auto arg3_upload = std::get<long>(curl_easy_setopt_fake_arg3_history[CURLOPT_UPLOAD]);
			REQUIRE(arg3_upload == 1L);
		}

		SUBCASE("FtpUploadTransferHandle CTOR clears CURLOPT_READFUNCTION on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_READFUNCTION);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const void *arg3_rFunc = std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_READFUNCTION]);
			REQUIRE(arg3_rFunc == NULL);
		}

		SUBCASE("FtpUploadTransferHandle CTOR initializes CURLOPT_READDATA on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_READDATA);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const FILE *arg3_rData = static_cast<FILE*>(std::get<void*>(curl_easy_setopt_fake_arg3_history[CURLOPT_READDATA]));
			REQUIRE(arg3_rData == AbstractFtpTransferHandleUnitTestHarness::file(transfer));
		}

		SUBCASE("FtpUploadTransferHandle CTOR initializes CURLOPT_INFILESIZE_LARGE on valid handle")
		{
			// GIVEN
			curl_easy_init_fake.return_val = dummyEasyHandlePtr;

			// WHEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// THEN
			const auto it = std::ranges::find(curl_easy_setopt_fake.arg1_history, CURLOPT_INFILESIZE_LARGE);
			REQUIRE(it != std::end(curl_easy_setopt_fake.arg1_history));

			const auto i = std::distance(curl_easy_setopt_fake.arg1_history, it);
			REQUIRE(curl_easy_setopt_fake.arg0_history[i] == dummyEasyHandlePtr);

			const curl_off_t arg3_fileSize = static_cast<curl_off_t>(std::get<long>(curl_easy_setopt_fake_arg3_history[CURLOPT_INFILESIZE_LARGE]));
			REQUIRE(arg3_fileSize == (curl_off_t)testFile.size());
		}
	}

	TEST_CASE("NetworkAccessManager::(un)registerTransfer()")
	{
		fff_setup();

		curl_multi_init_fake.return_val = dummyMultiHandlePtr;
		auto &networkAccessManager = NetworkAccessManager::instance();

		const auto url = Url("www.example.com");
		const auto testFilePath = std::filesystem::temp_directory_path().append("testFile.txt").string();
		auto testFile = File(testFilePath);

		SUBCASE("Register HTTP transfer calls curl_multi_add_handle()")
		{
			// GIVEN
			auto transfer = HttpTransferHandle(url);

			// WHEN
			networkAccessManager.registerTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_add_handle_fake.call_count == 1);
		}

		SUBCASE("Register FTP download transfer calls curl_multi_add_handle()")
		{
			// GIVEN
			auto transfer = FtpDownloadTransferHandle(testFile, url);

			// WHEN
			networkAccessManager.registerTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_add_handle_fake.call_count == 1);
		}

		SUBCASE("Register FTP upload transfer calls curl_multi_add_handle()")
		{
			// GIVEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// WHEN
			networkAccessManager.registerTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_add_handle_fake.call_count == 1);
		}

		SUBCASE("Unregister HTTP transfer calls curl_multi_remove_handle()")
		{
			// GIVEN
			auto transfer = HttpTransferHandle(url);

			// WHEN
			networkAccessManager.unregisterTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_remove_handle_fake.call_count == 1);
		}

		SUBCASE("Unregister FTP download transfer calls curl_multi_remove_handle()")
		{
			// GIVEN
			auto transfer = FtpDownloadTransferHandle(testFile, url);

			// WHEN
			networkAccessManager.unregisterTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_remove_handle_fake.call_count == 1);
		}

		SUBCASE("Unregister FTP upload transfer calls curl_multi_remove_handle()")
		{
			// GIVEN
			auto transfer = FtpUploadTransferHandle(testFile, url);

			// WHEN
			networkAccessManager.unregisterTransfer(transfer);

			// THEN
			REQUIRE(curl_multi_remove_handle_fake.call_count == 1);
		}
	}

	TEST_CASE("NetworkAccessManager::socketCallback()")
	{
		fff_setup();
		CurlDummyHandle dummyEasyHandle;
		void *dummyEasyHandlePtr = &dummyEasyHandle;
		NetworkAccessManagerUnitTestHarness unitTestHarness;

		SUBCASE("Registers FileDescriptorNotifier with type 'Read' for socket on CURL_POLL_IN event")
		{
			// GIVEN
			const int socket = 0;
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_IN);

			// THEN
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().readMap.size() == 1);
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().writeMap.size() == 0);

			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().readMap.at(0);
			REQUIRE(notifier->fileDescriptor() == socket);
			REQUIRE(notifier->type() == FileDescriptorNotifier::NotificationType::Read);
		}

		SUBCASE("Registers FileDescriptorNotifier with type 'Write' for socket on CURL_POLL_OUT event")
		{
			// GIVEN
			const int socket = 0;
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_OUT);

			// THEN
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().readMap.size() == 0);
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().writeMap.size() == 1);

			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().writeMap.at(0);
			REQUIRE(notifier->fileDescriptor() == socket);
			REQUIRE(notifier->type() == FileDescriptorNotifier::NotificationType::Write);
		}

		SUBCASE("Registers FileDescriptorNotifier with type 'Read' and 'Write' for socket on CURL_POLL_INOUT event")
		{
			// GIVEN
			const int socket = 0;
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_INOUT);

			// THEN
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().readMap.size() == 1);
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().writeMap.size() == 1);

			const auto &notifierRead = unitTestHarness.fileDescriptorNotifierRegistry().readMap.at(0);
			REQUIRE(notifierRead->fileDescriptor() == socket);
			REQUIRE(notifierRead->type() == FileDescriptorNotifier::NotificationType::Read);

			const auto &notifierWrite = unitTestHarness.fileDescriptorNotifierRegistry().writeMap.at(0);
			REQUIRE(notifierWrite->fileDescriptor() == socket);
			REQUIRE(notifierWrite->type() == FileDescriptorNotifier::NotificationType::Write);
		}

		SUBCASE("Does not register FileDescriptorNotifier on CURL_POLL_REMOVE event")
		{
			// GIVEN
			const int socket = 0;
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_REMOVE);

			// THEN
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().readMap.size() == 0);
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().writeMap.size() == 0);
		}

		SUBCASE("Unregisters all FileDescriptorNotifiers on CURL_POLL_REMOVE event")
		{
			// GIVEN
			const int socket = 0;
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_INOUT);

			// WHEN
			unitTestHarness.socketCallback(dummyEasyHandlePtr, socket, CURL_POLL_REMOVE);

			// THEN
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().readMap.size() == 0);
			REQUIRE(unitTestHarness.fileDescriptorNotifierRegistry().writeMap.size() == 0);
		}
	}

	TEST_CASE("NetworkAccessManager::timerCallback()")
	{
		fff_setup();
		NetworkAccessManagerUnitTestHarness unitTestHarness;
		const auto &timeoutTimer = unitTestHarness.timeoutTimer();

		SUBCASE("TimeoutTimer times out after requested timeout value")
		{
			// GIVEN
			const int timeoutMs = 1000;
			unitTestHarness.timerCallback(dummyMultiHandlePtr, timeoutMs);

			// THEN
			REQUIRE(timeoutTimer.running.get());
			app.processEvents(timeoutMs+10);
			REQUIRE_FALSE(timeoutTimer.running.get());
		}

		SUBCASE("TimeoutTimer times out ASAP, if immediate timeout requested")
		{
			// GIVEN
			unitTestHarness.timerCallback(dummyMultiHandlePtr, 1000);

			// WHEN
			unitTestHarness.timerCallback(dummyMultiHandlePtr, 0);

			// THEN
			REQUIRE(timeoutTimer.running.get());
			app.processEvents(1);
			REQUIRE_FALSE(timeoutTimer.running.get());
		}

		SUBCASE("TimeoutTimer is stopped, if requested")
		{
			// GIVEN
			unitTestHarness.timerCallback(dummyMultiHandlePtr, 1000);

			// WHEN
			unitTestHarness.timerCallback(dummyMultiHandlePtr, -1);

			// THEN
			REQUIRE_FALSE(timeoutTimer.running.get());
		}
	}

	TEST_CASE("NetworkAccessManager::onFileDescriptorNotifierTriggered()")
	{
		fff_setup();
		NetworkAccessManagerUnitTestHarness unitTestHarness;
		const int socket = 0;

		SUBCASE("When FileDescriptorNotifier fires, timeout timer is stopped")
		{
			// GIVEN
			unitTestHarness.timerCallback(dummyMultiHandlePtr, 1000);
			unitTestHarness.socketCallback(dummyMultiHandlePtr, socket, CURL_POLL_IN);
			REQUIRE(unitTestHarness.timeoutTimer().running.get());

			// WHEN
			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().readMap.at(0);
			notifier->triggered.emit(socket);

			// THEN
			REQUIRE_FALSE(unitTestHarness.timeoutTimer().running.get());
		}

		SUBCASE("When FileDescriptorNotifier with type 'Read' fires, curl_multi_socket_action() is called")
		{
			// GIVEN
			unitTestHarness.socketCallback(dummyMultiHandlePtr, socket, CURL_POLL_IN);

			// WHEN
			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().readMap.at(0);
			notifier->triggered.emit(socket);

			// THEN
			REQUIRE(curl_multi_socket_action_fake.call_count == 1);
			REQUIRE(curl_multi_socket_action_fake.arg0_val == dummyMultiHandlePtr);
			REQUIRE(curl_multi_socket_action_fake.arg1_val == socket);
			REQUIRE(curl_multi_socket_action_fake.arg2_val == CURL_CSELECT_IN);
		}

		SUBCASE("When FileDescriptorNotifier with type 'Write' fires, curl_multi_socket_action() is called")
		{
			// GIVEN
			unitTestHarness.socketCallback(dummyMultiHandlePtr, socket, CURL_POLL_OUT);

			// WHEN
			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().writeMap.at(0);
			notifier->triggered.emit(socket);

			// THEN
			REQUIRE(curl_multi_socket_action_fake.call_count == 1);
			REQUIRE(curl_multi_socket_action_fake.arg0_val == dummyMultiHandlePtr);
			REQUIRE(curl_multi_socket_action_fake.arg1_val == socket);
			REQUIRE(curl_multi_socket_action_fake.arg2_val == CURL_CSELECT_OUT);
		}

		SUBCASE("When FileDescriptorNotifier fires, curl_multi_info_read() is called")
		{
			// GIVEN
			unitTestHarness.socketCallback(dummyMultiHandlePtr, socket, CURL_POLL_IN);

			// WHEN
			const auto &notifier = unitTestHarness.fileDescriptorNotifierRegistry().readMap.at(0);
			notifier->triggered.emit(socket);

			// THEN
			REQUIRE(curl_multi_info_read_fake.call_count >= 1);
		}
	}

	TEST_CASE("NetworkAccessManager::onTimeoutTimerTriggered()")
	{
		fff_setup();
		NetworkAccessManagerUnitTestHarness unitTestHarness;

		SUBCASE("When TimeoutTimer fires, curl_multi_socket_action() is called")
		{
			// GIVEN
			unitTestHarness.timeoutTimer().timeout.emit();

			// THEN
			REQUIRE(curl_multi_socket_action_fake.call_count == 1);
			REQUIRE(curl_multi_socket_action_fake.arg0_val == dummyMultiHandlePtr);
			REQUIRE(curl_multi_socket_action_fake.arg1_val == CURL_SOCKET_TIMEOUT);
			REQUIRE(curl_multi_socket_action_fake.arg2_val == 0);
		}

		SUBCASE("When TimeoutTimer fires, curl_multi_info_read() is called")
		{
			// GIVEN
			unitTestHarness.timeoutTimer().timeout.emit();

			// THEN
			REQUIRE(curl_multi_info_read_fake.call_count >= 1);
		}
	}

	TEST_CASE("NetworkAccessManager::processTransferMessages()")
	{
		fff_setup();

		curl_multi_init_fake.return_val = dummyMultiHandlePtr;
		auto &networkAccessManager = NetworkAccessManager::instance();
		NetworkAccessManagerUnitTestHarness unitTestHarness;

		const auto url = Url("www.example.com");
		HttpTransferHandle transfer(url);

		// CURLINFO_PRIVATE is usually set in AbstractTransferHandle CTOR.
		// Since we use a stubbed libcurl that does not store private data,
		// we manually need to return private data here.
		// The private data is requested by AbstractTransferHandle::fromCurlEasyHandle,
		// which is called from NetworkAccessManager::processTransferMessages.
		curl_easy_getinfo_fake.custom_fake = [&](CURL*, CURLINFO info, va_list param) -> CURLcode {
			if (info == CURLINFO_PRIVATE) {
				auto abstractTransferHandle = va_arg(param, AbstractTransferHandle**);
				*abstractTransferHandle = &transfer;
			}
			return curl_easy_getinfo_fake.return_val;
		};

		// For each SUBCASE below return msgDone first and then a nullptr (no more msgs)
		CURLMsg msgDone { CURLMSG_DONE, transfer.handle(), { .result = CURLE_OK } };
		CURLMsg *msgReturnValues[2] = { &msgDone, nullptr };
		SET_RETURN_SEQ(curl_multi_info_read, msgReturnValues, 2);

		SUBCASE("When transfer is done, curl_multi_remove_handle() is called")
		{
			// GIVEN
			networkAccessManager.registerTransfer(transfer);

			// WHEN
			unitTestHarness.timeoutTimer().timeout.emit();

			// THEN
			REQUIRE(curl_multi_remove_handle_fake.call_count == 1);
			REQUIRE(curl_multi_remove_handle_fake.arg0_val == dummyMultiHandlePtr);
			REQUIRE(curl_multi_remove_handle_fake.arg1_val == transfer.handle());
		}

		SUBCASE("When transfer is done, AbstractTransferHandle::finished signal is emitted")
		{
			// GIVEN
			auto transferIsRunning = true;
			auto onTransferFinished = [&transferIsRunning](int result) {
				transferIsRunning = false;
			};

			transfer.finished.connect(onTransferFinished);
			networkAccessManager.registerTransfer(transfer);

			// WHEN
			unitTestHarness.timeoutTimer().timeout.emit();

			// THEN
			REQUIRE_FALSE(transferIsRunning);
		}
	}
}
