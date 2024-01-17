/*
 * This file implements C++ wrappers around the mosquitto C library.
 *
 * The classes implemented in this file differ from mosquittopp,
 * a wrapper class maintained in the Eclipse mosquitto repository,
 * in respect to the following aspects:
 * - library and client functions are separated into individual classes
 * - all functions are virtual for the purpose of convenient mocking
 * - some functions wrapped by mosquittopp are not wrapped here [1]
 * - some functions wrapped here are not wrapped by mosquittopp [1]
 * - mosquitto event callbacks are exposed as KDBindings::Signals
 * [1] as of January 2024
 */

#pragma once

#include <kdbindings/signal.h>
#include <mosquitto.h>
#include <string_view>

// TODO -> check once again what makes most sense to handle strings: const char *, const std::string &, std::string_view

/*
 * This is a C++ class wrapping library specific functions
 * of the mosquitto C library.
 */
class MosquittoLib
{
  private:
	MosquittoLib() = default;
	~MosquittoLib() = default;

	MosquittoLib(const MosquittoLib&) = delete;
	MosquittoLib &operator=(const MosquittoLib&) = delete;

  public:
	static MosquittoLib &instance() {
		static MosquittoLib instance;
		return instance;
	}

	virtual int init() {
		return mosquitto_lib_init();
	}

	virtual int cleanup() {
		return mosquitto_lib_cleanup();
	}

	virtual int version(int *major, int *minor, int *revision) const {
		return mosquitto_lib_version(major, minor, revision);
	}

	[[nodiscard]] virtual const char *connackString(int connack_code) const {
		return mosquitto_connack_string(connack_code);
	}

	[[nodiscard]] virtual const char *errorString(int mosq_errno) const {
		return mosquitto_strerror(mosq_errno);
	}

	[[nodiscard]] virtual const char *reasonString(int reason_code) const {
		return mosquitto_reason_string(reason_code);
	}

	[[nodiscard]] virtual bool isValidTopicNameForSubscription(std::string_view topic) const {
		// TODO
		// const auto result = mosquitto_sub_topic_check(topic.data());
		// const auto result = mosquitto_sub_topic_check2(topic.data(), topic.size());
		const auto result = mosquitto_validate_utf8(topic.data(), topic.length());
		return (result == MOSQ_ERR_SUCCESS);
	}
};

/*
 * This is a C++ class wrapping client specific functions
 * of the mosquitto C library.
 */
class MosquittoClient
{
  public:
	MosquittoClient(std::string_view clientId, bool clean_session = true) {
		m_clientInstance = mosquitto_new(clientId.data(), clean_session, this);

		mosquitto_connect_callback_set(m_clientInstance, onConnect);
		mosquitto_disconnect_callback_set(m_clientInstance, onDisconnect);
		mosquitto_publish_callback_set(m_clientInstance, onPublish);
		mosquitto_message_callback_set(m_clientInstance, onMessage);
		mosquitto_subscribe_callback_set(m_clientInstance, onSubscribe);
		mosquitto_unsubscribe_callback_set(m_clientInstance, onUnsubscribe);
		mosquitto_log_callback_set(m_clientInstance, onLog);
	}

	~MosquittoClient() {
		mosquitto_destroy(m_clientInstance);
	}

	virtual int connect(const char *host, int port, int keepalive) {
		return mosquitto_connect(m_clientInstance, host, port, keepalive);
	}

	virtual int disconnect() {
		return mosquitto_disconnect(m_clientInstance);
	}

	virtual int publish(int *msg_id, const char *topic, int payloadlen = 0, const void *payload = nullptr, int qos = 0, bool retain = false) {
		return mosquitto_publish(m_clientInstance, msg_id, topic, payloadlen, payload, qos, retain);
	}

	virtual int subscribe(int *msg_id, const char *sub, int qos = 0) {
		return mosquitto_subscribe(m_clientInstance, msg_id, sub, qos);
	}

	virtual int unsubscribe(int *msg_id, const char *sub) {
		return mosquitto_unsubscribe(m_clientInstance, msg_id, sub);
	}

	virtual int loopMisc() {
		return mosquitto_loop_misc(m_clientInstance);
	}

	virtual int loopRead(int max_packets = 1) {
		return mosquitto_loop_read(m_clientInstance, max_packets);
	}

	virtual int loopWrite(int max_packets = 1) {
		return mosquitto_loop_write(m_clientInstance, max_packets);
	}

	virtual int socket() {
		return mosquitto_socket(m_clientInstance);
	}

	virtual bool wantWrite() {
		return mosquitto_want_write(m_clientInstance);
	}

	virtual void *sslGet() {
		return mosquitto_ssl_get(m_clientInstance);
	}

	virtual int tlsSet(const char *cafile, const char *capath = nullptr, const char *certfile = nullptr, const char *keyfile = nullptr, int (*pw_callback)(char *buf, int size, int rwflag, void *userdata) = nullptr) {
		return mosquitto_tls_set(m_clientInstance, cafile, capath, certfile, keyfile, pw_callback);
	}

	virtual int usernamePasswordSet(const char *username, const char *password) {
		return mosquitto_username_pw_set(m_clientInstance, username, password);
	}

	virtual int willSet(const char *topic, int payloadlen = 0, const void *payload = nullptr, int qos = 0, bool retain = false) {
		return mosquitto_will_set(m_clientInstance, topic, payloadlen, payload, qos, retain);
	}

	KDBindings::Signal<int /*connackCode*/> connected;
	KDBindings::Signal<int /*reasonCode*/> disconnected;
	KDBindings::Signal<int /*msgId*/> published;
	KDBindings::Signal<const struct mosquitto_message * /*msg*/> message;
	KDBindings::Signal<int /*msgId*/, int /*qosCount*/, const int * /*grantedQos*/> subscribed;
	KDBindings::Signal<int /*msgId*/> unsubscribed;
	KDBindings::Signal<int /*level*/, const char * /*str*/> log;
	KDBindings::Signal<> error;

  private:
	static void onConnect([[maybe_unused]] struct mosquitto *client, void *self, int connackCode) {
		static_cast<MosquittoClient*>(self)->connected.emit(connackCode);
	}
	static void onDisconnect([[maybe_unused]] struct mosquitto *client, void *self, int reasonCode) {
		static_cast<MosquittoClient*>(self)->disconnected.emit(reasonCode);
	}
	static void onPublish([[maybe_unused]] struct mosquitto *client, void *self, int msgId) {
		static_cast<MosquittoClient*>(self)->published.emit(msgId);
	}
	static void onMessage([[maybe_unused]] struct mosquitto *client, void *self, const struct mosquitto_message *message) {
		static_cast<MosquittoClient*>(self)->message.emit(message);
	}
	static void onSubscribe([[maybe_unused]] struct mosquitto *client, void *self, int msg_id, int qos_count, const int *granted_qos) {
		static_cast<MosquittoClient*>(self)->subscribed.emit(msg_id, qos_count, granted_qos);
	}
	static void onUnsubscribe([[maybe_unused]] struct mosquitto *client, void *self, int msg_id) {
		static_cast<MosquittoClient*>(self)->unsubscribed.emit(msg_id);
	}
	static void onLog([[maybe_unused]] struct mosquitto *client, void *self, int level, const char *str) {
		static_cast<MosquittoClient*>(self)->log.emit(level, str);
	}
	static void onError([[maybe_unused]] struct mosquitto *client, void *self) {
		static_cast<MosquittoClient*>(self)->error.emit();
	}

	struct mosquitto *m_clientInstance;
};
