# GWSockets
WebSockets for GLua

# Usage
Place either `gmsv_gwsockets_win32.dll` (Windows) or `gmsv_gwsockets_linux.dll` (Linux) into you `GarrysMod/lua/bin` folder.

*NOTE:* Even though this module is mainly aimed at servers, it can also be used on clients. Just rename the module to `gmcl_gwsockets_os` and it will work on clientside as well.

You will also need to require the module in lua before you will be able to use it. You can do this running 

```LUA 
require("gwsockets")
```

# Documentation

### Connecting to a socket server
* First initialize a websocket instance using

  *NOTE:* URL's must include the scheme ( Either `ws://` or `wss://` )

  `Example: "wss://example.com:9999/api/socketserver"`

  ```LUA 
  GWSockets.createWebSocket( url, verifyCertificate=true )
  ```
  *NOTE:* If you want your websockets to use SSL but don't have a trusted certificate, you can set the second parameter to false.

* Next add any cookies or headers you would like to send with the initial request (Optional)

  ```LUA
  WEBSOCKET:setHeader( key, value )
  WEBSOCKET:setCookie( key, value )
  ```
  
* Add some callbacks (Optional)

  ```LUA
  -- called when a message from the peer has been received
  function WEBSOCKET:onMessage( msg )  end 
  
  -- called whenever anything goes wrong, this is always followed by a call to onDisconnected
  function WEBSOCKET:onError( errMessage ) end 
  
  -- called as soon as the socket is connected
  -- This is a good place to start sending messages
  function WEBSOCKET:onConnected() end 
  
  -- called whenever the socket has been disconnected
  -- this can either be because the socket has been requested to closed (either through user or error)
  -- or because the peer has closed the connection
  -- Note: If the peer does not close the connection gracefully, this might not be called until a write is attempted.
  function WEBSOCKET:onDisconnected() end 
  ```
  
* Lastly open the connection
  ```LUA
  WEBSOCKET:open()
  ```
  
* Once the socket has been opened you can send messages using the `write` function
  ```LUA
  WEBSOCKET:write( message )
  ```
  *NOTE:* You can write messages to the socket before the connection has been established and the socket
  will wait before sending them until the connection has been established. However, it is best practice
  to only start sending in the onConnected() callback.

* You can close the websocket connection at any time using `close` OR `closeNow`

  ```LUA
  WEBSOCKET:close()
  WEBSOCKET:closeNow()
  ```

  * `close` will wait for all queued messages to be sent and then gracefully close the connection
  * `closeNow` will immediately terminate the connection and discard all queued messages
  
* You can cancel any queued outbound messages by calling
  ```LUA
  WEBSOCKET:clearQueue()
  ```
* You can check if the websocket is connected using
  ```LUA
  WEBSOCKET:isConnected()
  ```
  *NOTE:* You should avoid using this and instead rely on the callbacks.

  
  
## Example:
```LUA
require("gwsockets")
local socket = GWSockets.createWebSocket("wss://echo.websocket.org/")

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
```

# Build
Requires premake5.
Run BuildProjects.sh or BuildProjects.bat with premake5 being installed.
Then use the appropriate generated solution for your system in the solutions/ folder and build the project.

### Windows
On Windows all you need to do is open the generated visual studio project and build the dll. All libraries and headers are provided already.

### Linux
On linux only essential programs for building C++ programs are required. On Ubuntu 64-bit these are:
```console
sudo apt-get install build-essential gcc-multilib g++-multilib
```

The required static libraries for linux are included in this repository to avoid  library/header version mismatching, but feel free to use your OS' libraries instead.

