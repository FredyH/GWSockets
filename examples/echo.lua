require("gwsockets")
local socket = GWSockets.createWebSocket("echo.websocket.org", "/", 80)
function socket:onMessage(txt) print("Recevied: ", txt) end
function socket:onError(txt) print(txt) end
function socket:onConnected() print("Connected to echo server") end
socket:open()
timer.Create("SocketTimer", 1, 0, function()
	print("Writing: ", "Echo")
	socket:write("Echo")
end)