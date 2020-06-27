#include "SSLWebSocket.h"
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/beast/websocket/ssl.hpp>

//TODO: I hope this is good enough, however I am not fully sure
//This function should provide nice error printing for failed certificate checks
//TODO: This does not check for revoked certificates yet, which is a huge pain in the ass to implement
//and of course both boost and openssl offer absolutely nothing to do this.
bool SSLWebSocket::verifyCertificate(bool preverified, boost::asio::ssl::verify_context& verifyContext)
{
	X509_STORE_CTX *ctx = verifyContext.native_handle();
	int error = X509_STORE_CTX_get_error(ctx);
	switch (error)
	{
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
		this->errorConnection("Unable to get issuer certificate.");
		return false;
	case X509_V_ERR_CERT_NOT_YET_VALID:
	case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
		this->errorConnection("Provided certificate not valid yet.");
		return false;
	case X509_V_ERR_CERT_REVOKED:
	case X509_V_ERR_CERT_HAS_EXPIRED:
	case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
		this->errorConnection("Provided certificate expired or revoked.");
		return false;
	case X509_V_ERR_INVALID_CA:
	case X509_V_ERR_SUBJECT_ISSUER_MISMATCH:
	case X509_V_ERR_AKID_SKID_MISMATCH:
	case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH:
	case X509_V_ERR_HOSTNAME_MISMATCH:
	case X509_V_ERR_EMAIL_MISMATCH:
	case X509_V_ERR_IP_ADDRESS_MISMATCH:
		this->errorConnection("Provided certificate had invalid details (Invalid CA, email, hostname, etc.)");
		return false;
	case X509_V_ERR_CERT_UNTRUSTED:
	case X509_V_ERR_CERT_REJECTED:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
		this->errorConnection("Provided certificate was self signed or untrusted.");
		return false;
	case X509_V_ERR_UNABLE_TO_GET_CRL:
		this->errorConnection("Unable to load CRL.");
		return false;
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
		this->errorConnection("Unable to find matching SSL root certificate locally.");
		return false;
	case X509_V_OK:
		return true;
	default:
		this->errorConnection("certificate verify failed");
		return false;
		break;
	}
}

void SSLWebSocket::asyncConnect(tcp::resolver::iterator it)
{
	this->resetWS();
	if (this->shouldVerifyCertificate)
	{
		this->getWS()->next_layer().set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
		this->getWS()->next_layer().set_verify_callback(boost::bind(&SSLWebSocket::verifyCertificate, this, boost::placeholders::_1, boost::placeholders::_2));
	}
	else
	{
		this->getWS()->next_layer().set_verify_mode(boost::asio::ssl::verify_none);
	}
	if (!SSL_set_tlsext_host_name(this->getSSL(), this->host.c_str()) ||
		!X509_VERIFY_PARAM_set1_host(SSL_get0_param(this->getSSL()), this->host.c_str(), 0))
	{
		this->errorConnection("Error setting SSL parameters");
	}
	else
	{
		boost::asio::async_connect(boost::beast::get_lowest_layer(*this->getWS()), it, boost::bind(&SSLWebSocket::socketConnected, this, boost::placeholders::_1, boost::placeholders::_2));
	}
}


void SSLWebSocket::asyncHandshake(std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	this->getWS()->next_layer().async_handshake(ssl::stream_base::client, boost::bind(&SSLWebSocket::sslHandshakeComplete, this, boost::placeholders::_1, host, path, decorator));
}

void SSLWebSocket::sslHandshakeComplete(const boost::system::error_code& ec, std::string host, std::string path, std::function<void(websocket::request_type&)> decorator)
{
	if (!ec)
	{
		this->getWS()->async_handshake_ex(host, path, decorator, boost::bind(&SSLWebSocket::handshakeCompleted, this, boost::placeholders::_1));
	}
	else
	{
		this->handshakeCompleted(ec);
	}
}

void SSLWebSocket::asyncRead()
{
	this->getWS()->async_read(this->readBuffer, boost::bind(&SSLWebSocket::onRead, this, boost::placeholders::_1, boost::placeholders::_2));
}

void SSLWebSocket::asyncWrite(std::string message)
{
	this->messageToWrite = message;
	this->getWS()->async_write(boost::asio::buffer(this->messageToWrite), boost::bind(&SSLWebSocket::onWrite, this, boost::placeholders::_1, boost::placeholders::_2));
}

void SSLWebSocket::closeSocket()
{
	websocket::stream<ssl::stream<tcp::socket>>* socket = this->getWS();
	if (socket != nullptr) {
		boost::beast::get_lowest_layer(*socket).close();
	}
}


void SSLWebSocket::asyncCloseSocket()
{
	this->getWS()->async_close(websocket::close_code::none, boost::bind(&SSLWebSocket::onDisconnected, this, boost::placeholders::_1));
}
