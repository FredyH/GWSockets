//
// Created by Fredy on 25.02.2018.
//

#include "GWSocket.h"

#include <sstream>
#include <string>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace ssl = boost::asio::ssl;
#include <algorithm>
#include <boost/beast/http/detail/rfc7230.hpp>
#include <utility>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;



void GWSocket::onDisconnected(const boost::system::error_code & ec)
{
	this->doClose(ec.message());
}

//Initiates an orderly, asynchronous shutdown of the connection
bool GWSocket::close()
{
	auto expected = this->state.load();
	if (expected == STATE_DISCONNECTED || expected == STATE_DISCONNECTING || expected == STATE_DISCONNECT_REQUESTED)
	{
		return false;
	}
	if (!this->state.compare_exchange_strong(expected, STATE_DISCONNECT_REQUESTED))
	{
		return false;
	}
	//To prevent recursive locking
	{
		std::lock_guard<std::recursive_mutex> guard(this->queueMutex);
		this->writeQueue.emplace_back(OUT_DISCONNECT);
	}
	//If we are connecting we do not have to checkWriting, because it will be done upon completion/error instead.
	if (expected != STATE_CONNECTING)
	{
		this->checkWriting();
	}
	return true;
}

//Closes the connection immediately and produces a disconnected message
void GWSocket::doClose(const std::string &disconnectReason)
{
	//Invalidate all completion handlers of the current connection attempt, see guardGeneration
	this->generation++;
	this->resolver.cancel();
	this->closeSocket();
	this->clearQueue();
	this->state = STATE_DISCONNECTED;
	this->writing = false;
	this->messageQueue.put(GWSocketMessageIn(IN_DISCONNECTED, disconnectReason));
}

//Tries to set the state to disconnecting using CAS
//returns true if it succeeded setting the state to disconnecting, which means the caller can proceed to close the socket
//returns false if it failed because the socket was already disconnected/disconnecting
bool GWSocket::setDisconnectingCAS()
{
	auto expected = this->state.load();
	if (expected == STATE_DISCONNECTED || expected == STATE_DISCONNECTING)
	{
		return false;
	}
	if (!this->state.compare_exchange_strong(expected, STATE_DISCONNECTING))
	{
		if (this->state == STATE_CONNECTED || this->state == STATE_CONNECTING)
		{
			//In this case it might've happened that the socket changed from connecting to connected state while this was called
			//If this happens we just want to try again
			return this->setDisconnectingCAS();
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool GWSocket::closeNow(const std::string &disconnectReason)
{
	if (!this->setDisconnectingCAS())
	{
		return false;
	}
	this->doClose(disconnectReason);
	return true;
}

//Returns true if the error means the connection was closed (by the peer or by us) rather than an actual failure.
static bool isConnectionClosedError(const boost::system::error_code &ec)
{
	return ec == boost::asio::error::eof
		|| ec == boost::asio::error::operation_aborted
		|| ec == boost::asio::ssl::error::stream_truncated
		|| ec == websocket::error::closed;
}

void GWSocket::onRead(const boost::system::error_code & ec, size_t readSize)
{
	if (!ec)
	{
		const auto data = boost::beast::make_printable(this->readBuffer.data());
		std::stringstream ss;
		ss << data;
		this->messageQueue.put(GWSocketMessageIn(IN_MESSAGE, ss.str()));
		this->readBuffer = boost::beast::multi_buffer();
		this->asyncRead();
	}
	else if (isConnectionClosedError(ec))
	{
		//This means the other side closed the connection, so close the socket
		std::string closeReason = this->getCloseReason();
		if (closeReason.empty()) {
			closeReason = "No reason specified";
		}
		this->closeNow(closeReason);
	}
	else
	{
		this->errorConnection(ec.message());
	}
}

bool GWSocket::errorConnection(const std::string &errorMessage)
{
	if (!this->setDisconnectingCAS())
	{
		return false;
	}
	this->messageQueue.put(GWSocketMessageIn(IN_ERROR, errorMessage));
	this->doClose(errorMessage);
	return true;
}

void GWSocket::handshakeCompleted(const boost::system::error_code &ec)
{
	if (!ec)
	{
		auto expected = STATE_CONNECTING;
		if (this->state.compare_exchange_strong(expected, STATE_CONNECTED))
		{
			this->messageQueue.put(GWSocketMessageIn(IN_CONNECTED, "Connected"));
			this->asyncRead();
			checkWriting();
		}
		else
		{
			//In this case the socket has been closed somewhere else, make sure that it is definitely closed
			//and socket does not end up in undefined state
			this->closeNow();
		}
	}
	else
	{
		this->errorConnection("Connection failed: " + ec.message());
	}
}

void GWSocket::socketConnected(const boost::system::error_code &ec)
{
	if (!ec)
	{
		auto hostWithPort = this->host;
		if (this->port != 80)
		{
			hostWithPort += ":" + std::to_string(this->port);
		}
		this->asyncHandshake(hostWithPort, this->path, [&](websocket::request_type& m)
		{
			if (!this->cookies.empty())
			{
				std::stringstream ss;
				bool first = true;
				for (const auto& pair : this->cookies)
				{
					auto key = pair.first;
					auto value = pair.second;
					if (!first) {
						ss << "; ";
					}
					first = false;
					ss << key << "=" << value;
				}
				m.insert(boost::beast::http::field::cookie, ss.str());
			}
			for (const auto& pair : this->headers)
			{
				auto key = pair.first;
				auto value = pair.second;
				m.insert(key, value);
			}
		});
	}
	else
	{
		this->errorConnection("Connection failed: " + ec.message());
	}
}


void GWSocket::hostResolvedStep(const boost::system::error_code &ec, tcp::resolver::results_type it)
{
	if (!ec)
	{
		this->asyncConnect(std::move(it));
	}
	else
	{
		this->errorConnection("[Resolver] " + ec.message());
	}
}

void GWSocket::open(bool shouldClearQueue)
{
	auto expected = STATE_DISCONNECTED;
	if (!this->state.compare_exchange_strong(expected, STATE_CONNECTING))
	{
		return;
	}

	// Clear the queue if it was requested.
	if (shouldClearQueue)
	{
		this->clearQueue();
	}

	//Start a new connection attempt, see guardGeneration
	this->generation++;

	// Look up the domain name
	this->resolver.async_resolve(host, std::to_string(port), this->guardGeneration([this](auto ec, auto results) { hostResolvedStep(ec, std::move(results)); }));
}

void GWSocket::checkWriting()
{
	std::unique_lock<std::recursive_mutex> guard(this->queueMutex);
	if ((this->state == STATE_CONNECTED || this->state == STATE_DISCONNECT_REQUESTED) && !writing && !this->writeQueue.empty())
	{
		this->writing = true;
		GWSocketMessageOut message = this->writeQueue.front();
		this->writeQueue.pop_front();
		switch (message.type)
		{
		case OUT_MESSAGE:
			this->asyncWrite(std::move(message.message), message.binary);
			break;
		case OUT_DISCONNECT:
		{
			auto expected = STATE_DISCONNECT_REQUESTED;
			if (this->state.compare_exchange_weak(expected, STATE_DISCONNECTING))
			{
				this->asyncCloseSocket();
			}
			else
			{
				//This is to prevent deadlocks in closeNow()
				guard.unlock();
				//If this case is reached it must be the case that the socket was closed elsewhere before.
				//This just ensures that we will definitely end up in a disconnected state
				this->closeNow();
			}
			break;
		}
		default:
			break;
		}
	}
}

void GWSocket::write(std::string message, bool binary)
{
	//To prevent recursive locking in checkWriting()
	{
		std::lock_guard<std::recursive_mutex> guard(this->queueMutex);
		this->writeQueue.emplace_back(OUT_MESSAGE, std::move(message), binary);
	}
	checkWriting();
}

void GWSocket::onWrite(const boost::system::error_code &ec, size_t bytesTransferred)
{
	if (!ec)
	{
		this->writing = false;
		checkWriting();
	}
	else if (isConnectionClosedError(ec))
	{
		//This means the other side closed the connection, so close the socket
		this->closeNow("Connection closed by remote host");
	}
	else
	{
		errorConnection(ec.message());
	}
}

void GWSocket::clearQueue()
{
	std::lock_guard<std::recursive_mutex> guard(this->queueMutex);
	this->writeQueue.clear();
}

//RFC 7230 token, used for header names and (RFC 6265) cookie names
//Source: https://greenbytes.de/tech/webdav/rfc7230.html#rule.token.separators
static bool isToken(const std::string &str)
{
	return !str.empty() && std::all_of(str.begin(), str.end(), [](const char c) { return boost::beast::http::detail::is_token_char(c) != 0; });
}

//RFC 7230 field-value: any octet except control characters (TAB allowed)
static bool isFieldValue(const std::string &str)
{
	return std::all_of(str.begin(), str.end(), [](const char c) { return boost::beast::http::detail::is_text(c) != 0; });
}

//RFC 6265 cookie-octet: printable ASCII excluding space, double quote, comma, semicolon and backslash
//Source: https://stackoverflow.com/questions/1969232/allowed-characters-in-cookies
static bool isCookieValue(const std::string &str)
{
	return std::all_of(str.begin(), str.end(), [](const char c) {
		return c >= 0x21 && c <= 0x7E && c != '"' && c != ',' && c != ';' && c != '\\';
	});
}

bool GWSocket::setCookie(const std::string &key, const std::string &value)
{
	if (!isToken(key) || !isCookieValue(value))
	{
		return false;
	}
	if (this->state != STATE_DISCONNECTED)
	{
		return false;
	}
	this->cookies[key] = value;
	return true;
}


bool GWSocket::setHeader(const std::string &key, const std::string &value)
{
	if (!isToken(key) || !isFieldValue(value))
	{
		return false;
	}
	if (this->state != STATE_DISCONNECTED)
	{
		return false;
	}
	this->headers[key] = value;
	return true;
}

void GWSocket::setPerMessageDeflate(bool value)
{
	perMessageDeflate = value;
}

void GWSocket::setDisableContextTakeover(bool value)
{
	disableContextTakeover = value;
}
