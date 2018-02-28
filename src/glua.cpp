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
static int weakMetaTable = 0;

void luaPrint(ILuaBase* LUA, std::string str) {
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "print");
	LUA->PushString(str.c_str());
	LUA->Call(1, 0);
}

template <typename T>
T* getCppObject(ILuaBase* LUA, int stackPos = 1) {
	LUA->GetField(stackPos, "__CppUserData");
	T* returnValue =  LUA->GetUserType<T>(-1, luaSocketMetaTable);
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
	socket->connect();
	return 0;
}

LUA_FUNCTION (createWebSocket) {
	LUA->CheckString(1);
	LUA->CheckString(2);
	LUA->CheckNumber(3);
	std::string host = LUA->GetString(1);
	std::string path = LUA->GetString(2);
	unsigned int port = LUA->GetNumber();
	GWSocket* socket = new GWSocket();
	LUA->CreateTable();
	LUA->PushUserType(socket, luaSocketMetaTable);
	LUA->SetField(-2, "__CppUserData");
	LUA->PushMetaTable(luaSocketMetaTable);
	LUA->SetMetaTable(-2);

	LUA->CreateTable();
	LUA->PushMetaTable(weakMetaTable);
	LUA->SetMetaTable(-2);
	LUA->Push(-2);
	LUA->SetField(-2, "value");
	int weakTableReference = LUA->ReferenceCreate();
	
	socketTableReferences[socket] = weakTableReference;
	socket->host = host;
	socket->path = path;
	socket->port = port;
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
	auto pair = std::begin(socketTableReferences);
	while (pair != std::end(socketTableReferences)) {
		auto socket = pair->first;
		auto weakTableReference = pair->second;
		auto messages = socket->messageQueue.clear();
		//Socket has been gced, and has been disconnected.
		//Do not process messages anymore since there clearly is not callbacks
		if (gcedSockets.count(socket) == 1) {
			//If the socket has been disconnected fully, delete it
			//Currently this should always be the case since closeNow() is called rather than close
			//but this might change in the future
			if (socket->canBeDeleted()) {
				delete socket;
				pair = socketTableReferences.erase(pair);
				gcedSockets.erase(socket);
			}
			else {
				pair++;
			}
			continue;
		}
		if (messages.empty()) {
			pair++;
			continue;
		}
		LUA->ReferencePush(weakTableReference);
		LUA->GetField(-1, "value");
		if (LUA->GetType(-1) == Type::NIL) {
			LUA->Pop(2);
			pair++;
			continue;
		}
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
		LUA->Pop(2);
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

	weakMetaTable = LUA->CreateMetaTable("WSWeakTable");
	LUA->PushString("v");
	LUA->SetField(-2, "__mode");
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
	socket.connect("echo.websocket.org", "/", 80);
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