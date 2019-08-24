//
// Created by Fredy on 25.02.2018.
//

#include "GWSocket.h"

#include <sstream>
#include <string>
#include <boost/beast/core.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/bind.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace ssl = boost::asio::ssl;
#include <regex>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;



void GWSocket::onDisconnected(const boost::system::error_code & ec)
{
	this->doClose();
}

//Initiates an orderly, asynchronous shutdown of the connection
bool GWSocket::close()
{
	auto expected = this->state.load();
	if (expected == STATE_DISCONNECTED || expected == STATE_DISCONNECTING || expected == SATE_DISCONNECT_REQUESTED)
	{
		return false;
	}
	if (!this->state.compare_exchange_strong(expected, SATE_DISCONNECT_REQUESTED))
	{
		return false;
	}
	//To prevent recursive locking
	{
		std::lock_guard<std::recursive_mutex> guard(this->queueMutex);
		this->writeQueue.emplace_back(OUT_DISCONNECT);
	}
	this->checkWriting();
	return true;
}

//Closes the connection immediately and produces a disconnected message
void GWSocket::doClose()
{
	this->closeSocket();
	this->clearQueue();
	this->state = STATE_DISCONNECTED;
	this->writing = false;
	this->messageQueue.put(GWSocketMessageIn(IN_DISCONNECTED));
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

bool GWSocket::closeNow()
{
	if (!this->setDisconnectingCAS())
	{
		return false;
	}
	this->doClose();
	return true;
}

void GWSocket::onRead(const boost::system::error_code & ec, size_t readSize)
{
	if (!ec)
	{
		auto data = boost::beast::make_printable(this->readBuffer.data());
		std::stringstream ss;
		ss << data;
		this->messageQueue.put(GWSocketMessageIn(IN_MESSAGE, ss.str()));
		this->readBuffer = boost::beast::multi_buffer();
		this->asyncRead();
	}
	else if(ec == boost::asio::error::eof || ec == boost::asio::error::operation_aborted || boost::asio::ssl::error::stream_truncated)
	{
		//This means the other side closed the connection, so close the socket
		this->closeNow();
	}
	else
	{
		this->errorConnection(ec.message());
	}
}

bool GWSocket::errorConnection(std::string errorMessage)
{
	if (!this->setDisconnectingCAS())
	{
		return false;
	}
	this->messageQueue.put(GWSocketMessageIn(IN_ERROR, errorMessage));
	this->doClose();
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

void GWSocket::socketConnected(const boost::system::error_code &ec, tcp::resolver::iterator it)
{
	if (!ec)
	{
		auto host = this->host;
		if (this->port != 80)
		{
			host += ":" + std::to_string(this->port);
		}
		this->asyncHandshake(host, this->path, [&](websocket::request_type& m)
		{
			if (!this->cookies.empty())
			{
				std::stringstream ss;
				bool first = true;
				for (auto pair : this->cookies)
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
			for (auto pair : this->headers)
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


void GWSocket::hostResolvedStep(const boost::system::error_code &ec, tcp::resolver::iterator it)
{
	if (!ec)
	{
		this->asyncConnect(it);
	}
	else
	{
		this->errorConnection("[Resolver] " + ec.message());
	}
}

void GWSocket::open()
{
	auto expected = STATE_DISCONNECTED;
	if (!this->state.compare_exchange_strong(expected, STATE_CONNECTING))
	{
		return;
	}
	this->clearQueue();
	// Look up the domain name
	tcp::resolver::query q{ host, std::to_string(port) };
	this->resolver.async_resolve(q, boost::bind(&GWSocket::hostResolvedStep, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void GWSocket::checkWriting()
{
	std::unique_lock<std::recursive_mutex> guard(this->queueMutex);
	if ((this->state == STATE_CONNECTED || this->state == SATE_DISCONNECT_REQUESTED) && !writing && !this->writeQueue.empty())
	{
		this->writing = true;
		GWSocketMessageOut message = this->writeQueue.front();
		this->writeQueue.pop_front();
		switch (message.type)
		{
		case OUT_MESSAGE:
			this->asyncWrite(message.message);
			break;
		case OUT_DISCONNECT:
		{
			auto expected = SATE_DISCONNECT_REQUESTED;
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

void GWSocket::write(std::string message)
{
	//To prevent recursive locking in checkWriting()
	{
		std::lock_guard<std::recursive_mutex> guard(this->queueMutex);
		this->writeQueue.emplace_back(OUT_MESSAGE, message);
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
	else if (ec == boost::asio::error::eof || ec == boost::asio::error::operation_aborted || boost::asio::ssl::error::stream_truncated)
	{
		//This means the other side closed the connection, so close the socket
		this->closeNow();
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


//Source: https://stackoverflow.com/questions/1969232/allowed-characters-in-cookies
static std::regex cookieNameRegex(R"(^[\w\!#\$%&'\*\+\-\.\^_`\|~]+$)");
static std::regex cookieValueRegex(R"(^[\w\!#\$%&'\(\)\*\+\-\./\:\<\=\>\?@\[\]\^_`\{\|\}~]*$)");
bool GWSocket::setCookie(std::string key, std::string value)
{
	if (!std::regex_match(key, cookieNameRegex) || !std::regex_match(value, cookieValueRegex))
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


//Source: https://greenbytes.de/tech/webdav/rfc7230.html#rule.token.separators
static std::regex headerRegex(R"(^[\w\!#\$%'\*\+\-\.\^_`\|~]*$)");
bool GWSocket::setHeader(std::string key, std::string value)
{
	if (!std::regex_match(key, headerRegex) || key.empty() || !std::regex_match(value, headerRegex))
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