//
// Created by Fredy on 25.02.2018.
//
#ifndef GWSOCKETS_GWSOCKET_H
#define GWSOCKETS_GWSOCKET_H
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <GWSocket.h>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>
#include "BlockingQueue.h"

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

enum GWSMessageType { TYPE_CONNECTED, TYPE_MESSAGE, TYPE_ERROR };

class GWSocketMessage
{
public:
	GWSMessageType type;
	std::string message;
	GWSocketMessage(GWSMessageType type, std::string message): type(type), message(message) { }
};

enum SocketState
{
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_DISCONNECTING,
	STATE_DISCONNECTED,
};

class GWSocket
{
public:
	GWSocket(std::string host, unsigned short port, std::string path) : host(host), port(port), path(path) { };
	virtual ~GWSocket() { };

	void open();
	void onDisconnected(const boost::system::error_code & ec);
	void close();
	void closeNow();
	void write(std::string message);
	void setCookie(std::string key, std::string value);
	void setHeader(std::string key, std::string value);
	BlockingQueue<GWSocketMessage> messageQueue;
	bool isConnected() { return state == STATE_CONNECTED; };
	bool canBeDeleted() { return state == STATE_DISCONNECTED; };
	std::string path;
	std::string host;
	unsigned int port;
	std::atomic<SocketState> state{ STATE_DISCONNECTED };

	static std::unique_ptr<boost::asio::io_context> ioc;
protected:
	//Has to call the socketReady function when successfully done
	virtual void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator) = 0;
	void handshakeCompleted(const boost::system::error_code &ec);
	//Has to call the socketConnected function when successfully done
	virtual void asyncConnect(tcp::resolver::iterator it) = 0;
	void socketConnected(const boost::system::error_code& ec, tcp::resolver::iterator i);
	//Has to call the onRead function when something is read
	virtual void asyncRead() = 0;
	//Has to call the onWrite function when something is written
	virtual void asyncWrite(std::string message) = 0;
	virtual void asyncCloseSocket() = 0;
	virtual void closeSocket() = 0;
	void errorConnection(std::string errorMessage);
	void onRead(const boost::system::error_code &ec, size_t readSize);
	void onWrite(const boost::system::error_code &ec, size_t bytesTransferred);
	void checkWriting();
	void hostResolvedStep(const boost::system::error_code &ec, tcp::resolver::iterator it);
	bool writing = { false };
	std::unordered_map<std::string, std::string> cookies;
	std::unordered_map<std::string, std::string> headers;
	//websocket::stream<tcp::socket> ws{ *ioc };
	tcp::resolver resolver{ *ioc };
	boost::beast::multi_buffer readBuffer;
	std::vector<std::string> writeQueue;
	//This mutex is currently completely unnecessary since everything runs in the server's main thread
	//I included it here and in the code anyways if we ever want to change this
	std::mutex queueMutex;
};


#endif //GWSOCKETS_GWSOCKET_H
