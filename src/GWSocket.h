//
// Created by Fredy on 25.02.2018.
//
#ifndef GWSOCKETS_GWSOCKET_H
#define GWSOCKETS_GWSOCKET_H
#define BOOST_BEAST_ALLOW_DEPRECATED
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "BlockingQueue.h"

//This entire implementations is written with the idea in mind that the IO worker might
//be ran in a different thread than the server's main thread. This is currently not the case, which
//is why some functions might seem a bit overprotective when it comes to avoiding race condition that can't
//even happen when only using a single thread.

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

enum GWSMessageInType { IN_CONNECTED, IN_MESSAGE, IN_ERROR, IN_DISCONNECTED };

class GWSocketMessageIn
{
public:
	GWSMessageInType type;
	std::string message;
	GWSocketMessageIn(GWSMessageInType type, std::string message): type(type), message(message) { }
	GWSocketMessageIn(GWSMessageInType type) : GWSocketMessageIn(type, "") { }
};

enum GWSMessageOutType { OUT_DISCONNECT, OUT_MESSAGE };

class GWSocketMessageOut
{
public:
	GWSMessageOutType type;
	std::string message;
	GWSocketMessageOut(GWSMessageOutType type, std::string message) : type(type), message(message) { }
	GWSocketMessageOut(GWSMessageOutType type) : GWSocketMessageOut(type, "") { }
};

enum SocketState
{
	STATE_CONNECTING, // Started connecting to the server, i.e. the TCP connection/handshakes are being done
	STATE_CONNECTED, //The connection is open and communication is possible
	SATE_DISCONNECT_REQUESTED, //The socket has been requested to close orderly using the close method
	STATE_DISCONNECTING, //The socket is currently in the process of disconnecting
	STATE_DISCONNECTED //The socket is fully disconnected, be it after disconnecting or the initial state
};

class GWSocket
{
public:
	GWSocket(std::string host, unsigned short port, std::string path) : host(host), port(port), path(path) { };
	virtual ~GWSocket() { };

	void open();
	void onDisconnected(const boost::system::error_code & ec);
	bool close();
	bool closeNow();
	void write(std::string message);
	bool setCookie(std::string key, std::string value);
	bool setHeader(std::string key, std::string value);
	BlockingQueue<GWSocketMessageIn> messageQueue;
	bool isConnected() { return state == STATE_CONNECTED; };
	bool canBeDeleted() { return state == STATE_DISCONNECTED; };
	void clearQueue();
	std::string path;
	std::string host;
	unsigned int port;
	std::atomic<SocketState> state{ STATE_DISCONNECTED };

	static std::unique_ptr<boost::asio::io_context> ioc; //Needs to be initialized on module load
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
	bool errorConnection(std::string errorMessage);
	void onRead(const boost::system::error_code &ec, size_t readSize);
	void onWrite(const boost::system::error_code &ec, size_t bytesTransferred);
	void checkWriting();
	void hostResolvedStep(const boost::system::error_code &ec, tcp::resolver::iterator it);
	bool writing = { false };
	void doClose();
	bool setDisconnectingCAS();
	std::unordered_map<std::string, std::string> cookies;
	std::unordered_map<std::string, std::string> headers;
	tcp::resolver resolver{ *ioc };
	boost::beast::multi_buffer readBuffer;
	std::deque<GWSocketMessageOut> writeQueue;
	//This mutex is currently completely unnecessary since everything runs in the server's main thread
	//I included it here and in the code anyways if we ever want to change this
	std::recursive_mutex queueMutex;
};


#endif //GWSOCKETS_GWSOCKET_H
