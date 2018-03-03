#include "SSLWebSocket.h"
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/beast/websocket/ssl.hpp>

ssl::context SSLWebSocket::sslContext(ssl::context::sslv23_client);

void SSLWebSocket::asyncConnect(tcp::resolver::iterator it)
{
	boost::asio::async_connect(this->ws.next_layer().next_layer(), it, boost::bind(&SSLWebSocket::socketConnected, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}


void SSLWebSocket::asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	ws.next_layer().async_handshake(ssl::stream_base::client, boost::bind(&SSLWebSocket::sslHandshakeComplete, this, boost::asio::placeholders::error, host, path, decorator));
}

void SSLWebSocket::sslHandshakeComplete(const boost::system::error_code& ec, std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	if (!ec)
	{
		ws.async_handshake_ex(host, path, decorator, boost::bind(&SSLWebSocket::handshakeCompleted, this, boost::asio::placeholders::error));
	}
	else
	{
		this->errorConnection("Error during SSL handshake: " + ec.message());
	}
}

void SSLWebSocket::asyncRead()
{
	this->ws.async_read(this->readBuffer, boost::bind(&SSLWebSocket::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void SSLWebSocket::asyncWrite(std::string message)
{
	this->ws.async_write(boost::asio::buffer(message), boost::bind(&SSLWebSocket::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void SSLWebSocket::closeSocket()
{
	this->ws.lowest_layer().close();
}

void SSLWebSocket::asyncCloseSocket()
{
	this->ws.async_close(websocket::close_code::none, boost::bind(&SSLWebSocket::onDisconnected, this, boost::asio::placeholders::error));
}