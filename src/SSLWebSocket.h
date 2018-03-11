
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
protected:
	void asyncConnect(tcp::resolver::iterator it);
	void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	void asyncRead();
	void asyncWrite(std::string message);
	void asyncCloseSocket();
	void closeSocket();
	void sslHandshakeComplete(const boost::system::error_code& ec, std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	bool verifyCertificate(bool preverified, boost::asio::ssl::verify_context& ctx);
	websocket::stream<ssl::stream<tcp::socket>> ws{ *ioc, *sslContext };
private:
	SSL* getSSL() { return this->ws.next_layer().native_handle(); }
};

#endif //GWSOCKETS_SSLWEBSOCKET_H
