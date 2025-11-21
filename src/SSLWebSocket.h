
#ifndef GWSOCKETS_SSLWEBSOCKET_H
#define GWSOCKETS_SSLWEBSOCKET_H
#include "GWSocket.h"
#include <boost/asio/ssl/stream.hpp>

namespace ssl = boost::asio::ssl;
class SSLWebSocket : public GWSocket
{
public:
	SSLWebSocket(const std::string &host, const unsigned short port, const std::string &path) : GWSocket(host, port, path) { }
	bool shouldVerifyCertificate = true;
	static std::unique_ptr<ssl::context> sslContext; //Needs to be initialized on module load
	~SSLWebSocket() override {
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
	void sslHandshakeComplete(const boost::system::error_code& ec, const std::string &host, const std::string &path, const std::function<void(websocket::request_type&)>& decorator);
	bool verifyCertificate(bool preverified, boost::asio::ssl::verify_context& ctx);
	std::atomic<websocket::stream<ssl::stream<tcp::socket>, true>*> ws{ nullptr };
	//This is not an atomic function, it only ensures visibility.
	//Callers have to make sure that atomicity is not required/ensured otherwise
	void resetWS()
	{
		const auto stream = ws.exchange(nullptr);
		delete stream;

		const auto newWS = new websocket::stream<ssl::stream<tcp::socket>, true>(*ioc, *sslContext);

		// Set permessage-deflate options for message compression if requested.
		websocket::permessage_deflate opts;
		opts.client_enable = perMessageDeflate;
		opts.client_no_context_takeover = disableContextTakeover;
		newWS->set_option(opts);

		ws = newWS;
	}

	websocket::stream<ssl::stream<tcp::socket>>* getWS() const {
		return this->ws.load();
	}
	std::string getCloseReason() override
	{
		const websocket::stream<ssl::stream<tcp::socket>>* socket = this->getWS();
		if (socket != nullptr) {
			auto reason = socket->reason();
			return { reason.reason.begin(), reason.reason.end() };
		}
		return "";
	}
private:
	SSL* getSSL() const { return this->getWS()->next_layer().native_handle(); }
};

#endif //GWSOCKETS_SSLWEBSOCKET_H
