#include "WebSocket.h"


void WebSocket::asyncConnect(const tcp::resolver::results_type it)
{
	this->resetWS();
	boost::asio::async_connect(this->getWS()->next_layer(), it, [this](auto ec, const auto&) { socketConnected(ec); });
}


void WebSocket::asyncHandshake(const std::string host, const std::string path, std::function<void(websocket::request_type&)> decorator)
{
	this->getWS()->set_option(websocket::stream_base::decorator(decorator));
	this->getWS()->async_handshake(host, path, [this](auto ec) { handshakeCompleted(ec); });
}

void WebSocket::asyncRead()
{
	this->getWS()->async_read(this->readBuffer, [this](auto ec, auto bytes_transferred) { onRead(ec, bytes_transferred); });
}

void WebSocket::asyncWrite(std::string message, const bool isBinary)
{
	this->messageToWrite = std::move(message);

	auto* ws_ptr = this->getWS();
	ws_ptr->binary(isBinary);

	ws_ptr->async_write(boost::asio::buffer(this->messageToWrite), [this](auto ec, auto bytes_transferred) { onWrite(ec, bytes_transferred); });
}

void WebSocket::closeSocket()
{
	websocket::stream<tcp::socket>* socket = this->getWS();
	if (socket != nullptr) {
		boost::beast::get_lowest_layer(*socket).close();
	}
}

void WebSocket::asyncCloseSocket()
{
	this->getWS()->async_close(websocket::close_code::none, [this](auto ec) { onDisconnected(ec); });
}
