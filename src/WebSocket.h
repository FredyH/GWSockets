#ifndef GWSOCKETS_WEBSOCKET_H
#define GWSOCKETS_WEBSOCKET_H
#include "GWSocket.h"

class WebSocket : public GWSocket
{
public:
	WebSocket(std::string host, unsigned short port, std::string path) : GWSocket(host, port, path) { }
	virtual ~WebSocket()
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
	std::atomic<websocket::stream<tcp::socket>*> ws;
	//This is not an atomic function, it only ensures visibility.
	//Callers have to make sure that atomicity is not required/ensured otherwise
	void resetWS()
	{
		auto stream = ws.exchange(nullptr);
		if (stream != nullptr)
		{
			delete ws;
		}
		ws = new websocket::stream<tcp::socket>(*ioc);
	}
	websocket::stream<tcp::socket>* getWS()
	{
		return this->ws.load();
	}
};


#endif //GWSOCKETS_WEBSOCKET_H
