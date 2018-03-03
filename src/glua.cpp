#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <boost/chrono/chrono.hpp>
#include "GWSocket.h"
#include <thread>
#include <deque>
#include "GarrysMod/Lua/Interface.h"
#include "GarrysMod/Lua/Types.h"
#include <unordered_map>
#include <unordered_set>

using namespace GarrysMod::Lua;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

static std::unordered_set<GWSocket*> gcedSockets = std::unordered_set<GWSocket*>();
static std::unordered_map<GWSocket*, int> socketTableReferences = std::unordered_map<GWSocket*, int>();

static int userDataMetatable = 0;
static int luaSocketMetaTable = 0;

void luaPrint(ILuaBase* LUA, std::string str) {
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "print");
	LUA->PushString(str.c_str());
	LUA->Call(1, 0);
}

template <typename T>
T* getCppObject(ILuaBase* LUA, int stackPos = 1) {
	LUA->GetField(stackPos, "__CppUserData");
	T* returnValue =  LUA->GetUserType<T>(-1, userDataMetatable);
	if (returnValue == nullptr) {
		LUA->ThrowError("[GWSockets] Expected GWSocket table");
	}
	return returnValue;
}

LUA_FUNCTION(socketCloseNow) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	socket->closeNow();
	return 0;
}

LUA_FUNCTION(socketClose) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	socket->close();
	return 0;
}

LUA_FUNCTION (socketWrite) {
	LUA->CheckString(2);
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	unsigned int len;
	const char* str = LUA->GetString(2, &len);
	socket->write(std::string(str, len));
	return 0;
}

LUA_FUNCTION(socketOpen) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED) {
		LUA->ThrowError("Cannot open socket that is already connected");
	}
	//As soon as the socket starts connecting we want to keep a reference to the table so that it does not
	//get garbage collected, so that the callbacks can be called.
	LUA->Push(1);
	socketTableReferences[socket] = LUA->ReferenceCreate();
	socket->open();
	return 0;
}

LUA_FUNCTION(socketSetCookie) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED) {
		LUA->ThrowError("Cannot set a cookie for an already connected websocket");
	}
	LUA->CheckString(2);
	LUA->CheckString(3);
	auto key = std::string(LUA->GetString(2));
	auto value = std::string(LUA->GetString(3));
	socket->setCookie(key, value);
	return 0;
}

LUA_FUNCTION(socketSetHeader) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	if (socket->state != STATE_DISCONNECTED) {
		LUA->ThrowError("Cannot set header for an already connected websocket");
	}
	LUA->CheckString(2);
	LUA->CheckString(3);
	auto key = std::string(LUA->GetString(2));
	auto value = std::string(LUA->GetString(3));
	socket->setHeader(key, value);
	return 0;
}

LUA_FUNCTION(socketIsConnected) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	LUA->PushBool(socket->isConnected());
	return 1;
}

LUA_FUNCTION (createWebSocket) {
	LUA->CheckString(1);
	LUA->CheckString(2);
	LUA->CheckNumber(3);
	std::string host = LUA->GetString(1);
	std::string path = LUA->GetString(2);
	unsigned int port = LUA->GetNumber();
	GWSocket* socket = new GWSocket();
	socket->host = host;
	socket->path = path;
	socket->port = port;

	LUA->CreateTable();

	LUA->PushUserType(socket, userDataMetatable);
	LUA->SetField(-2, "__CppUserData");

	LUA->PushMetaTable(luaSocketMetaTable);
	LUA->SetMetaTable(-2);
	return 1;
}

void pcall(ILuaBase* LUA, int numArgs) {
	if (LUA->PCall(numArgs, 0, 0)) {
		const char* err = LUA->GetString(-1);
		LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "ErrorNoHalt");
		//In case someone removes ErrorNoHalt this doesn't break everything
		if (!LUA->IsType(-1, GarrysMod::Lua::Type::FUNCTION)) {
			LUA->Pop(2);
			return;
		}
		LUA->PushString(err);
		LUA->PushString("\n");
		LUA->Call(2, 0);
		LUA->Pop(2);
	}
}

LUA_FUNCTION(webSocketThink) {
	GWSocket::ioc->poll();
	auto it = std::begin(gcedSockets);
	while (it != std::end(gcedSockets)) {
		auto socket = *it;
		//If a gced socket has been disconnected, it is safe to delete.
		if (socket->state == STATE_DISCONNECTED) {
			delete socket;
			it = gcedSockets.erase(it);
		}
		else {
			it++;
		}
	}
	auto pair = std::begin(socketTableReferences);
	while (pair != std::end(socketTableReferences)) {
		auto socket = pair->first;
		auto tableReference = pair->second;
		auto messages = socket->messageQueue.clear();
		if (socket->state == STATE_DISCONNECTED) {
			//This means the socket has been disconnected (possibly from the other side)
			//We drop the reference to the table here so that the websocket can be gced
			LUA->ReferenceFree(tableReference);
			pair = socketTableReferences.erase(pair);
			continue;
		}
		if (messages.empty()) {
			pair++;
			continue;
		}
		LUA->ReferencePush(tableReference);
		int tableIndex = LUA->Top();
		for (auto message : messages) {
			switch (message.type)
			{
			case TYPE_ERROR:
				LUA->GetField(-1, "onError");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			case TYPE_CONNECTED:
				LUA->GetField(-1, "onConnected");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			case TYPE_MESSAGE:
				LUA->GetField(-1, "onMessage");
				LUA->Push(tableIndex);
				LUA->PushString(message.message.c_str());
				break;
			default:
				luaPrint(LUA, "[GWSockets] Wrong messagetype found");
			}
			if (LUA->IsType(-3, Type::FUNCTION)) {
				pcall(LUA, 2);
			}
			else {
				LUA->Pop(3);
			}
		}
		//Pops the socket's table
		LUA->Pop();
		pair++;
	}
	return 0;
}

LUA_FUNCTION(socketToString) {
	GWSocket* socket = getCppObject<GWSocket>(LUA);
	std::stringstream ss;
	ss << "GWSocket " << socket;
	LUA->PushString(ss.str().c_str());
	return 1;
}

LUA_FUNCTION(socketGCFunction) {
	GWSocket* socket = LUA->GetUserType<GWSocket>(1, userDataMetatable);
	auto pair = socketTableReferences.find(socket);
	//Realistically this should not happen since if there is a reference to the table a cyclic
	//dependency exists preventing the table and the userdata to be gced
	if (pair != socketTableReferences.end()) {
		socketTableReferences.erase(pair);
	}
	gcedSockets.insert(socket);
	socket->closeNow();
	return 0;
}

GMOD_MODULE_OPEN() {
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

	return 1;
}

GMOD_MODULE_CLOSE() {
	GWSocket::ioc->stop();
	for (auto pair : socketTableReferences) {
		pair.first->closeNow();
		delete pair.first;
	}
	socketTableReferences.clear();
	gcedSockets.clear();
	return 0;
}


//==================================================================================
// The code below is just used to test the module independently from gmod
//==================================================================================

void runIOThread() {
	while (!GWSocket::ioc->stopped()) {
		GWSocket::ioc->run();
	}
}

void sendMessages(GWSocket* socket) {
	while (!GWSocket::ioc->stopped()) {
		socket->write("PUFFS");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	std::cout << "MESSAGE THREAD ENDED" << std::endl;
}

// Sends a WebSocket message and prints the response
int main()
{
	GWSocket socket;
	socket.open("echo.websocket.org", "/", 80);
	std::thread t1(runIOThread);
	std::thread t2(sendMessages, &socket);
	for (int i = 0; i < 10; i++) {
		std::deque<GWSocketMessage> messages = socket.messageQueue.clear();
		for (auto message : messages) {
			std::cout << "Message received: " << message.message << std::endl;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	std::cout << "Socket close" << std::endl;
	socket.closeNow();
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::cout << "Ending IO thread" << std::endl;
	GWSocket::ioc->stop();
	t1.join();
	t2.join();
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return EXIT_SUCCESS;
}