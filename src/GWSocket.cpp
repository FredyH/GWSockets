//
// Created by Fredy on 25.02.2018.
//

#include "GWSocket.h"

#include <iostream>
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

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

std::unique_ptr<boost::asio::io_context> GWSocket::ioc(new boost::asio::io_context());


void GWSocket::onDisconnected(const boost::system::error_code & ec)
{
	this->state = STATE_DISCONNECTED;
	this->writing = true;
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
	this->closeNow();
	this->messageQueue.put(GWSocketMessage(TYPE_ERROR, errorMessage));
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

//Source: http://www.zedwood.com/article/cpp-urlencode-function
//It's really annoying how c++/boost does not have this
static std::string urlencode(const std::string &s)
{
	static const char lookup[] = "0123456789abcdef";
	std::stringstream e;
	for (size_t i = 0, ix = s.length(); i < ix; i++)
	{
		const char& c = s[i];
		if ((48 <= c && c <= 57) ||//0-9
			(65 <= c && c <= 90) ||//abc...xyz
			(97 <= c && c <= 122) || //ABC...XYZ
			(c == '-' || c == '_' || c == '.' || c == '~')
			)
		{
			e << c;
		}
		else
		{
			e << '%';
			e << lookup[(c & 0xF0) >> 4];
			e << lookup[(c & 0x0F)];
		}
	}
	return e.str();
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
					ss << urlencode(key) << "=" << urlencode(value);
				}
				m.insert(boost::beast::http::field::cookie, ss.str());
			}
			for (auto pair : this->headers)
			{
				auto key = pair.first;
				auto value = pair.second;
				m.insert(urlencode(key), urlencode(value));
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
	if (this->isConnected() && !writing && !this->writeQueue.empty())
	{
		this->writing = true;
		std::string message = this->writeQueue.back();
		this->writeQueue.pop_back();
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


void GWSocket::setCookie(std::string key, std::string value)
{
	if (this->state != STATE_DISCONNECTED)
	{
		return;
	}
	this->cookies[key] = value;
}


void GWSocket::setHeader(std::string key, std::string value)
{
	if (this->state != STATE_DISCONNECTED)
	{
		return;
	}
	this->headers[key] = value;
}