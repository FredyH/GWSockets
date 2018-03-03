#include "WebSocket.h"

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>


void WebSocket::asyncConnect(tcp::resolver::iterator it)
{
	boost::asio::async_connect(this->ws.next_layer(), it, boost::bind(&WebSocket::socketConnected, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}


void WebSocket::asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	ws.async_handshake_ex(host, path, decorator, boost::bind(&WebSocket::handshakeCompleted, this, boost::asio::placeholders::error));
}

void WebSocket::asyncRead()
{
	this->ws.async_read(this->readBuffer, boost::bind(&WebSocket::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void WebSocket::asyncWrite(std::string message)
{
	this->ws.async_write(boost::asio::buffer(message), boost::bind(&WebSocket::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void WebSocket::closeSocket()
{
	this->ws.lowest_layer().close();
}

void WebSocket::asyncCloseSocket()
{
	this->ws.async_close(websocket::close_code::none, boost::bind(&WebSocket::onDisconnected, this, boost::asio::placeholders::error));
}