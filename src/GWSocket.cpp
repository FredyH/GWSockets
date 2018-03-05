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
#include <regex>

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;



void GWSocket::onDisconnected(const boost::system::error_code & ec)
{
	this->state = STATE_DISCONNECTED;
	this->writing = false;
}

void GWSocket::close()
{
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING)
	{
		return;
	}
	this->state = STATE_DISCONNECTING;
	this->checkWriting();
}

void GWSocket::closeNow()
{
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING)
	{
		return;
	}
	this->state = STATE_DISCONNECTED;
	this->closeSocket();
	std::lock_guard<std::mutex> guard(this->queueMutex);
	this->writing = false;
	this->writeQueue.clear();
}

void GWSocket::onRead(const boost::system::error_code & ec, size_t readSize)
{
	if (!ec)
	{
		auto data = boost::beast::buffers(this->readBuffer.data());
		std::stringstream ss;
		ss << data;
		this->messageQueue.put(GWSocketMessage(TYPE_MESSAGE, ss.str()));
		this->readBuffer = boost::beast::multi_buffer();
		this->asyncRead();
	} else
	{
		this->errorConnection(ec.message());
	}
}

void GWSocket::errorConnection(std::string errorMessage)
{
	if (this->state == STATE_DISCONNECTED || this->state == STATE_DISCONNECTING)
	{
		return;
	}
	this->messageQueue.put(GWSocketMessage(TYPE_ERROR, errorMessage));
	this->closeNow();
}

void GWSocket::handshakeCompleted(const boost::system::error_code &ec)
{
	if (!ec)
	{
		this->state = STATE_CONNECTED;
		this->messageQueue.put(GWSocketMessage(TYPE_CONNECTED, "Connected"));
		this->asyncRead();
		checkWriting();
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
	if (this->state != STATE_DISCONNECTED)
	{
		return;
	}
	this->state = STATE_CONNECTING;
	// Look up the domain name
	tcp::resolver::query q{ host, std::to_string(port) };
	this->resolver.async_resolve(q, boost::bind(&GWSocket::hostResolvedStep, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void GWSocket::checkWriting()
{
	std::lock_guard<std::mutex> guard(this->queueMutex);
	if ((this->state == STATE_CONNECTED || this->state == STATE_DISCONNECTING) && !writing && !this->writeQueue.empty())
	{
		this->writing = true;
		std::string message = this->writeQueue.front();
		this->writeQueue.pop_front();
		this->asyncWrite(message);
	}
	else if (!writing && this->state == STATE_DISCONNECTING)
	{
		this->writing = true;
		this->asyncCloseSocket();
	}
}

void GWSocket::write(std::string message)
{
	//To prevent recursive locking in checkWriting()
	{
		std::lock_guard<std::mutex> guard(this->queueMutex);
		this->writeQueue.push_back(message);
	}
	checkWriting();
}

void GWSocket::onWrite(const boost::system::error_code &ec, size_t bytesTransferred)
{
	if (ec)
	{
		errorConnection("[Writing] " + ec.message());
	}
	else
	{
		this->writing = false;
		checkWriting();
	}
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