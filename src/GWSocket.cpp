//
// Created by Fredy on 25.02.2018.
//

#include "GWSocket.h"

#include <boost/beast/core.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/bind.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/ip/tcp.hpp>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

//boost::asio::io_context GWSocket::ioc();
std::unique_ptr<boost::asio::io_context> GWSocket::ioc(new boost::asio::io_context());


void GWSocket::onDisconnected(const boost::system::error_code & ec) {
	this->state = STATE_DISCONNECTED;
	this->writing = true;
}

void GWSocket::close() {
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING) {
		return;
	}
	this->state = STATE_DISCONNECTING;
	this->checkWriting();
}

void GWSocket::closeNow() {
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING) {
		return;
	}
	this->state = STATE_DISCONNECTED;
	this->ws.next_layer().close();
	std::lock_guard<std::mutex> guard(this->queueMutex);
	this->writing = false;
	this->writeQueue.clear();
}

void GWSocket::onRead(const boost::system::error_code & ec, size_t readSize) {
	if (!ec) {
		auto data = boost::beast::buffers(this->readBuffer.data());
		std::stringstream ss;
		ss << data;
		this->messageQueue.put(GWSocketMessage(TYPE_MESSAGE, ss.str()));
		this->readBuffer = boost::beast::multi_buffer();
		this->ws.async_read(this->readBuffer, boost::bind(&GWSocket::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	} else {
		this->errorConnection(ec.message());
	}
}

void GWSocket::errorConnection(std::string errorMessage) {
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING) {
		return;
	}
	this->closeNow();
	this->messageQueue.put(GWSocketMessage(TYPE_ERROR, errorMessage));
}

void GWSocket::handshakeStep(const boost::system::error_code &ec) {
	if (!ec) {
		this->state = STATE_CONNECTED;
		this->messageQueue.put(GWSocketMessage(TYPE_CONNECTED, "Connected"));
		this->ws.async_read(this->readBuffer, boost::bind(&GWSocket::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		checkWriting();
	}
	else {
		this->errorConnection("Connection failed: " + ec.message());
	}
}

void GWSocket::connectedStep(const boost::system::error_code &ec, tcp::resolver::iterator it) {
	if (!ec) {
		this->ws.async_handshake(this->host, this->path, boost::bind(&GWSocket::handshakeStep, this, boost::asio::placeholders::error));
	}
	else {
		this->errorConnection("Connection failed: " + ec.message());
	}
}


void GWSocket::hostResolvedStep(const boost::system::error_code &ec, tcp::resolver::iterator it) {
	if (!ec) {
		tcp::resolver::iterator end;
		boost::asio::async_connect(this->ws.next_layer(), it, boost::bind(&GWSocket::connectedStep, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
	}
	else {
		this->errorConnection("[Resolver] " + ec.message());
	}
}

void GWSocket::connect() {
	this->connect(this->host, this->path, this->port);
}

void GWSocket::connect(std::string host, std::string path, unsigned short port) {
	if (this->state != STATE_DISCONNECTED) {
		return;
	}
	this->state = STATE_CONNECTING;
	this->path = path;
	this->host = host;
	this->port = port;
	// Look up the domain name
	tcp::resolver::query q{ host, std::to_string(port) };
	this->resolver.async_resolve(q, boost::bind(&GWSocket::hostResolvedStep, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void GWSocket::checkWriting() {
	std::lock_guard<std::mutex> guard(this->queueMutex);
	if (this->isConnected() && !writing && !this->writeQueue.empty()) {
		this->writing = true;
		std::string message = this->writeQueue.back();
		this->writeQueue.pop_back();
		this->ws.async_write(boost::asio::buffer(message), boost::bind(&GWSocket::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}
	else if (!writing && this->state == STATE_DISCONNECTING) {
		this->writing = true;
		this->ws.async_close(websocket::close_code::none, boost::bind(&GWSocket::onDisconnected, this, boost::asio::placeholders::error));
	}
}

void GWSocket::write(std::string message) {
	//To prevent recursive locking in checkWriting()
	{
		std::lock_guard<std::mutex> guard(this->queueMutex);
		this->writeQueue.push_back(message);
	}
	checkWriting();
}

void GWSocket::onWrite(const boost::system::error_code &ec, size_t bytesTransferred) {
	if (ec) {
		errorConnection("[Writing] " + ec.message());
	}
	else {
		this->writing = false;
		checkWriting();
	}
}