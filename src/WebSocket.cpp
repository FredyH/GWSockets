#include "WebSocket.h"

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>


void WebSocket::asyncConnect(tcp::resolver::iterator it)
{
	this->resetWS();
	boost::asio::async_connect(this->getWS()->next_layer(), it, boost::bind(&WebSocket::socketConnected, this, boost::placeholders::_1, boost::placeholders::_2));
}


void WebSocket::asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	this->getWS()->async_handshake_ex(host, path, decorator, boost::bind(&WebSocket::handshakeCompleted, this, boost::placeholders::_1));
}

void WebSocket::asyncRead()
{
	this->getWS()->async_read(this->readBuffer, boost::bind(&WebSocket::onRead, this, boost::placeholders::_1, boost::placeholders::_2));
}

void WebSocket::asyncWrite(std::string message)
{
	this->getWS()->async_write(boost::asio::buffer(message), boost::bind(&WebSocket::onWrite, this, boost::placeholders::_1, boost::placeholders::_2));
}

void WebSocket::closeSocket()
{
	boost::beast::get_lowest_layer(*this->getWS()).close();
}

void WebSocket::asyncCloseSocket()
{
	this->getWS()->async_close(websocket::close_code::none, boost::bind(&WebSocket::onDisconnected, this, boost::placeholders::_1));
}