
#ifndef GWSOCKETS_SSLWEBSOCKET_H
#define GWSOCKETS_SSLWEBSOCKET_H
#include "GWSocket.h"
#include <boost/asio/ssl/stream.hpp>

namespace ssl = boost::asio::ssl;
class SSLWebSocket : public GWSocket
{
public:
	SSLWebSocket(std::string host, unsigned short port, std::string path) : GWSocket(host, port, path) { }
	bool shouldVerifyCertificate = true;
	static std::unique_ptr<ssl::context> sslContext; //Needs to be initialized on module load
	virtual ~SSLWebSocket()
	{ 
		auto stream = ws.exchange(nullptr);
		if (stream != nullptr)
		{
			delete stream;
		}
	};
protected:
	void asyncConnect(tcp::resolver::iterator it);
	void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	void asyncRead();
	void asyncWrite(std::string message);
	void asyncCloseSocket();
	void closeSocket();
	void sslHandshakeComplete(const boost::system::error_code& ec, std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	bool verifyCertificate(bool preverified, boost::asio::ssl::verify_context& ctx);
	std::atomic<websocket::stream<ssl::stream<tcp::socket>>*> ws{ nullptr };
	//This is not an atomic function, it only ensures visibility.
	//Callers have to make sure that atomicity is not required/ensured otherwise
	void resetWS()
	{
		auto stream = ws.exchange(nullptr);
		if (stream != nullptr)
		{
			delete ws;
		}
		ws = new websocket::stream<ssl::stream<tcp::socket>>(*ioc, *sslContext);
	}
	websocket::stream<ssl::stream<tcp::socket>>* getWS()
	{
		return this->ws.load();
	}
private:
	SSL* getSSL() { return this->getWS()->next_layer().native_handle(); }
};

#endif //GWSOCKETS_SSLWEBSOCKET_H
