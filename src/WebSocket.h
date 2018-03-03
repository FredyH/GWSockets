#ifndef GWSOCKETS_WEBSOCKET_H
#define GWSOCKETS_WEBSOCKET_H
#include "GWSocket.h"

class WebSocket : public GWSocket
{
public:
	WebSocket(std::string host, unsigned short port, std::string path) : GWSocket(host, port, path) { }

	void asyncConnect(tcp::resolver::iterator it);
	void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	void asyncRead();
	void asyncWrite(std::string message);
	void asyncCloseSocket();
	void closeSocket();
	websocket::stream<tcp::socket> ws{ *ioc };
};


#endif //GWSOCKETS_WEBSOCKET_H
