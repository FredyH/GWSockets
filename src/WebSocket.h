#ifndef GWSOCKETS_WEBSOCKET_H
#define GWSOCKETS_WEBSOCKET_H
#include "GWSocket.h"

class WebSocket : public GWSocket
{
public:
	WebSocket(const std::string &host, const unsigned short port, const std::string &path) : GWSocket(host, port, path) { }

	~WebSocket() override {
		const auto stream = ws.exchange(nullptr);
		delete stream;
	};
protected:
	void asyncConnect(tcp::resolver::results_type it) override;
	void asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator) override;
	void asyncRead() override;
	void asyncWrite(std::string message, bool isBinary) override;
	void asyncCloseSocket() override;
	void closeSocket() override;
	std::string messageToWrite;
	std::atomic<websocket::stream<tcp::socket>*> ws{ nullptr };
	//This is not an atomic function, it only ensures visibility.
	//Callers have to make sure that atomicity is not required/ensured otherwise
	void resetWS()
	{
		const auto stream = ws.exchange(nullptr);
		delete stream;

		const auto newWS = new websocket::stream<tcp::socket, true>(*ioc);

		// Set permessage-deflate options for message compression if requested.
		websocket::permessage_deflate opts;
		opts.client_enable = perMessageDeflate;
		opts.client_no_context_takeover = disableContextTakeover;
		newWS->set_option(opts);

		ws = newWS;
	}

	websocket::stream<tcp::socket>* getWS() const {
		return this->ws.load();
	}
	std::string getCloseReason() override
	{
		const websocket::stream<tcp::socket>* socket = this->getWS();
		if (socket != nullptr) {
			auto reason = socket->reason();
			return { reason.reason.begin(), reason.reason.end() };
		}
		return "";
	}
};


#endif //GWSOCKETS_WEBSOCKET_H
