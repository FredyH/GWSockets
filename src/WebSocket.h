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
	void asyncConnect(tcp::resolver::results_type it);
	void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator);
	void asyncRead();
	void asyncWrite(std::string message);
	void asyncCloseSocket();
	void closeSocket();
	std::string messageToWrite = "";
	std::atomic<websocket::stream<tcp::socket>*> ws{ nullptr };
	//This is not an atomic function, it only ensures visibility.
	//Callers have to make sure that atomicity is not required/ensured otherwise
	void resetWS()
	{
		auto stream = ws.exchange(nullptr);
		if (stream != nullptr)
		{
			delete ws;
		}

		auto newWS = new websocket::stream<tcp::socket, true>(*ioc);

		// Set permessage-deflate options for message compression if requested.
		websocket::permessage_deflate opts;
		opts.client_enable = perMessageDeflate;
		opts.client_no_context_takeover = disableContextTakeover;
		newWS->set_option(opts);

		ws = newWS;
	}

	websocket::stream<tcp::socket>* getWS()
	{
		return this->ws.load();
	}
};


#endif //GWSOCKETS_WEBSOCKET_H
