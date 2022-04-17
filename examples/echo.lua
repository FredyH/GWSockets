require("gwsockets")
local socket = GWSockets.createWebSocket("wss://echo.websocket.events/")

function socket:onMessage(txt)
	print("Received: ", txt)
end

function socket:onError(txt)
	print("Error: ", txt)
end

-- We start writing only after being connected here. Technically this is not required as this library
-- just waits until the socket is connected before sending, but it's probably good practice
function socket:onConnected()
	print("Connected to echo server")
	-- Write Echo once every second, 10 times
	timer.Create("SocketWriteTimer", 1, 0, function()
		print("Writing: ", "Echo")
		socket:write("Echo")
	end)
	timer.Simple(10, function()
		timer.Remove("SocketWriteTimer")
		-- Even if some of the messages have not reached the other side yet, this type of close makes sure
		-- to only close the socket once all queued messages have been received by the peer.
		socket:close()
	end)
end

function socket:onDisconnected()
	print("WebSocket disconnected")
end

socket:open()