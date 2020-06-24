#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <boost/chrono/chrono.hpp>
#include <thread>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include "GarrysMod/Lua/Interface.h"
#include "GarrysMod/Lua/Types.h"
#include "GWSocket.h"
#include "WebSocket.h"
#include "SSLWebSocket.h"
#include "Url.hpp"
#include "UpdateChecker.h"

using namespace GarrysMod::Lua;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

static std::unordered_set<GWSocket*> gcedSockets = std::unordered_set<GWSocket*>();
static std::unordered_map<GWSocket*, int> socketTableReferences = std::unordered_map<GWSocket*, int>();

static int userDataMetatable = 0;
static int luaSocketMetaTable = 0;

std::unique_ptr<boost::asio::io_context> GWSocket::ioc{};
std::unique_ptr<boost::asio::ssl::context> SSLWebSocket::sslContext{};

#ifdef WIN32
#include <wincrypt.h>
static void loadRootCertificates()
{
	HCERTSTORE hStore = CertOpenSystemStore(0, L"ROOT");
	if (hStore == NULL) {
		return;
	}
	X509_STORE *store = X509_STORE_new();
	PCCERT_CONTEXT pContext = NULL;
	while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
		X509 *x509 = d2i_X509(NULL,
			(const unsigned char **)&pContext->pbCertEncoded,
			pContext->cbCertEncoded);
		if (x509 != NULL) {
			X509_STORE_add_cert(store, x509);
			X509_free(x509);
		}
	}
	CertFreeCertificateContext(pContext);
	CertCloseStore(hStore, 0);
	SSL_CTX_set_cert_store(SSLWebSocket::sslContext->native_handle(), store);
}
#else
static void loadRootCertificates()
{
	SSLWebSocket::sslContext->set_default_verify_paths();
}
#endif

static void initialize() {
	//I am initializing them here every time the module loads, since otherwise they seem to contain bad values after a map change
	GWSocket::ioc.reset(new boost::asio::io_context());
	//This does not mean that the client only uses SSLV2/3 apparently, rather it is "Generic SSL/TLS"
	SSLWebSocket::sslContext.reset(new boost::asio::ssl::context(ssl::context::sslv23));
	loadRootCertificates();
}

static void deinitialize() {
	//This prevents memory leaking since the static variables seem to not ever be deleted
	GWSocket::ioc.release();
	SSLWebSocket::sslContext.release();
}


void luaPrint(ILuaBase* LUA, std::string str)
{
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "print");
	LUA->PushString(str.c_str());
	LUA->Call(1, 0);
}

void throwErrorNoHalt( ILuaBase* LUA, std::string str )
{
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "ErrorNoHalt");
    //In case someone removes ErrorNoHalt this doesn't break everything
    if (!LUA->IsType(-1, GarrysMod::Lua::Type::FUNCTION))
    {
        LUA->Pop(2);
        return;
    }
    LUA->PushString(str.c_str());
    LUA->PushString("\n");
    LUA->Call(2, 0);
    LUA->Pop(2);
}

// Returns a GWSocket object using data parsed from the url passed. Will return null if the passed url is not valid
static GWSocket* createWebSocketFromURL(std::string urlString, bool verifyCertificate)
{
    Url url(urlString); // Create a url object that will parse our URL

    // Get the required information from the url
    std::string host = url.host();
    std::string path = url.path().empty() ? "/" : url.path();
    bool useSSL = (url.scheme() == "https" || url.scheme() == "wss");
    unsigned short port = url.port().empty() ? (useSSL ? 443 : 80) : std::stoi(url.port());

    if(host.empty()) 
	{
        throw std::invalid_argument("Invalid url passed. Make sure it includes a scheme");
    }

	if (useSSL)
	{
		SSLWebSocket* socket =  new SSLWebSocket(host, port, path);
		socket->shouldVerifyCertificate = verifyCertificate;
		return socket;
	}
	else
	{
		return new WebSocket(host, port, path);
	}
}

template <typename T>
T* getCppObject(ILuaBase* LUA, int stackPos = 1)
{
	LUA->GetField(stackPos, "__CppUserData");
	T* returnValue =  LUA->GetUserType<T>(-1, userDataMetatable);
	if (returnValue == nullptr) {
		LUA->ThrowError("[GWSockets] Expected GWSocket table");
	}
	return returnValue;
}

LUA_FUNCTION(socketCloseNow)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	socket->closeNow();
	return 0;
}

LUA_FUNCTION(socketClose)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	socket->close();
	return 0;
}

LUA_FUNCTION (socketWrite)
{
	LUA->CheckString(2);
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	unsigned int len;
	const char* str = LUA->GetString(2, &len);
	socket->write(std::string(str, len));
	return 0;
}

LUA_FUNCTION(socketOpen)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED)
	{
		LUA->ThrowError("Cannot open socket that is already connected");
	}
	//As soon as the socket starts connecting we want to keep a reference to the table so that it does not
	//get garbage collected, so that the callbacks can be called.
	if (socketTableReferences.find(socket) == socketTableReferences.end())
	{
		LUA->Push(1);
		socketTableReferences[socket] = LUA->ReferenceCreate();
	}
	socket->open();
	return 0;
}

LUA_FUNCTION(socketClearQueue)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	socket->clearQueue();
	return 0;
}

LUA_FUNCTION(socketSetCookie)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED)
	{
		LUA->ThrowError("Cannot set a cookie for an already connected websocket");
	}
	LUA->CheckString(2);
	LUA->CheckString(3);
	if (!socket->setCookie(LUA->GetString(2), LUA->GetString(3)))
	{
		LUA->ThrowError("Invalid cookie name or value");
	}
	return 0;
}

LUA_FUNCTION(socketSetHeader)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED)
	{
		LUA->ThrowError("Cannot set header for an already connected websocket");
	}
	LUA->CheckString(2);
	LUA->CheckString(3);
	if (!socket->setHeader(LUA->GetString(2), LUA->GetString(3)))
	{
		LUA->ThrowError("Invalid header name or value");
	}
	return 0;
}

LUA_FUNCTION(socketIsConnected)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	LUA->PushBool(socket->isConnected());
	return 1;
}

LUA_FUNCTION (createWebSocket)
{
	LUA->CheckString(1);
	std::string urlString = LUA->GetString(1);
    GWSocket *socket;
    try
	{
		bool verifyCertificate = LUA->IsType(2, Type::BOOL) ? LUA->GetBool(2) : true;
        socket = createWebSocketFromURL(urlString, verifyCertificate);
        LUA->CreateTable();

        LUA->PushUserType(socket, userDataMetatable);
        LUA->SetField(-2, "__CppUserData");

        LUA->PushMetaTable(luaSocketMetaTable);
        LUA->SetMetaTable(-2);
        return 1;
    }
	catch(std::invalid_argument &e)
	{
        // The url was bad so we should throw an error now
        throwErrorNoHalt(LUA, "Unable to create WebSocket! Invalid URL. Refer to the documentation for the proper URL format.");
        return 0;
    }
}

void pcall(ILuaBase* LUA, int numArgs)
{
	if (LUA->PCall(numArgs, 0, 0))
	{
		const char* err = LUA->GetString(-1);
        throwErrorNoHalt(LUA, err);
	}
}

LUA_FUNCTION(webSocketThink)
{
	if (GWSocket::ioc->stopped()) {
		GWSocket::ioc->restart();
	}
	GWSocket::ioc->poll();
	auto it = std::begin(gcedSockets);
	while (it != std::end(gcedSockets))
	{
		auto socket = *it;
		//If a gced socket has been disconnected, it is safe to delete.
		if (socket->state == STATE_DISCONNECTED)
		{
			delete socket;
			it = gcedSockets.erase(it);
		}
		else
		{
			it++;
		}
	}
	auto pair = std::begin(socketTableReferences);
	while (pair != std::end(socketTableReferences))
	{
		auto socket = pair->first;
		auto tableReference = pair->second;
		auto messages = socket->messageQueue.clear();
		if (messages.empty() && socket->state != STATE_DISCONNECTED)
		{
			pair++;
			continue;
		}
		LUA->ReferencePush(tableReference);
		int tableIndex = LUA->Top();
		for (auto message : messages)
		{
			switch (message.type)
			{
			case IN_ERROR:
				LUA->GetField(-1, "onError");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			case IN_CONNECTED:
				LUA->GetField(-1, "onConnected");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			case IN_MESSAGE:
				LUA->GetField(-1, "onMessage");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			case IN_DISCONNECTED:
				LUA->GetField(-1, "onDisconnected");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			default:
				luaPrint(LUA, "[GWSockets] Wrong messagetype found");
			}
			if (LUA->IsType(-3, Type::FUNCTION))
			{
				pcall(LUA, 2);
			}
			else
			{
				LUA->Pop(3);
			}
		}
		//Pops the socket's table
		LUA->Pop();
		//The size == 0 check here is required because a callback might trigger more messages in the callback
		if (socket->state == STATE_DISCONNECTED && messages.size() == 0)
		{
			//This means the socket has been disconnected (possibly from the other side)
			//We drop the reference to the table here so that the websocket can be gced
			LUA->ReferenceFree(tableReference);
			pair = socketTableReferences.erase(pair);
		}
		else
		{
			pair++;
		}
	}
	return 0;
}

LUA_FUNCTION(socketToString)
{
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	std::stringstream ss;
	ss << "GWSocket " << socket;
	LUA->PushString(ss.str().c_str());
	return 1;
}

LUA_FUNCTION(socketGCFunction)
{
	GWSocket* socket = LUA->GetUserType<GWSocket>(1, userDataMetatable);
	auto pair = socketTableReferences.find(socket);
	//Realistically this should not happen since if there is a reference to the table a cyclic
	//dependency exists preventing the table and the userdata to be gced
	if (pair != socketTableReferences.end())
	{
		socketTableReferences.erase(pair);
	}
	gcedSockets.insert(socket);
	socket->closeNow();
	return 0;
}

GMOD_MODULE_OPEN()
{
	initialize();
	//Adds all the GWSocket functions
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->CreateTable();
	LUA->PushCFunction(createWebSocket);
	LUA->SetField(-2, "createWebSocket");
	LUA->SetField(-2, "GWSockets");
	LUA->Pop();

	//Adds the think hook that calls the callbacks
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "hook");
	LUA->GetField(-1, "Add");
	LUA->PushString("Think");
	LUA->PushString("__GWSocketThink");
	LUA->PushCFunction(webSocketThink);
	LUA->Call(3, 0);
	LUA->Pop();
	LUA->Pop();

	//Prototype table
	LUA->CreateTable();
	LUA->PushCFunction(socketOpen);
	LUA->SetField(-2, "open");
	LUA->PushCFunction(socketWrite);
	LUA->SetField(-2, "write");
	LUA->PushCFunction(socketClose);
	LUA->SetField(-2, "close");
	LUA->PushCFunction(socketCloseNow);
	LUA->SetField(-2, "closeNow");
	LUA->PushCFunction(socketSetCookie);
	LUA->SetField(-2, "setCookie");
	LUA->PushCFunction(socketSetHeader);
	LUA->SetField(-2, "setHeader");
	LUA->PushCFunction(socketIsConnected);
	LUA->SetField(-2, "isConnected");
	LUA->PushCFunction(socketClearQueue);
	LUA->SetField(-2, "clearQueue");

	//Actual metatable
	luaSocketMetaTable = LUA->CreateMetaTable("WebSocket");
	LUA->Push(-2);
	LUA->SetField(-2, "__index");
	LUA->PushCFunction(socketToString);
	LUA->SetField(-2, "__tostring");
	LUA->Pop();

	//Pop prototype table
	LUA->Pop();

	userDataMetatable = LUA->CreateMetaTable("UserDataMetatable");
	LUA->PushCFunction(socketGCFunction);
	LUA->SetField(-2, "__gc");
	LUA->Pop();

	LUA->PushCFunction(UpdateChecker::doVersionCheck);
	LUA->Call(0, 0);

	return 1;
}

GMOD_MODULE_CLOSE()
{

	for (auto pair : socketTableReferences)
	{
		pair.first->close();
	}
	//Give sockets one second to close their connection gracefully
	//Note: If no sockets are open or all of them are done this will return earlier
	GWSocket::ioc->run_for(std::chrono::seconds(1));
	GWSocket::ioc->stop();
	//Anything that has not closed by now will be forcefully closed
	for (auto pair : socketTableReferences)
	{
		pair.first->closeNow();
		delete pair.first;
	}
	socketTableReferences.clear();
	gcedSockets.clear();
	deinitialize();
	return 0;
}


//==================================================================================
// The code below is just used to test the module independently from gmod
//==================================================================================

void runIOThread()
{
	while (!GWSocket::ioc->stopped())
	{
		GWSocket::ioc->run();
	}
}

void sendMessages(GWSocket* socket)
{
	while (!GWSocket::ioc->stopped())
	{
		socket->write("PUFFS");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	std::cout << "MESSAGE THREAD ENDED" << std::endl;
}


// Sends a WebSocket message and prints the response
int main()
{
	initialize();
	try
	{
		GWSocket *socket = createWebSocketFromURL("wss://echo.websocket.org", true);
		socket->open();
		std::thread t1(runIOThread);
		std::thread t2(sendMessages, socket);
		for (int i = 0; i < 100; i++)
		{
			std::deque<GWSocketMessageIn> messages = socket->messageQueue.clear();
			for (auto message : messages)
			{
				std::cout << "Message received: " << message.message << std::endl;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << "Socket close" << std::endl;
		socket->closeNow();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		std::cout << "Ending IO thread" << std::endl;
		GWSocket::ioc->stop();
		t1.join();
		t2.join();
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		return EXIT_SUCCESS;
	}
	catch (std::invalid_argument &e)
	{
		std::cout << "Invalid websocket url. Unable to continue. Make sure you included a scheme in the url ( wss or ws )";
		return 0;
	}
}
