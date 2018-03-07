# GWSockets
WebSockets for GLua

# THIS IS STILL A WIP
Do not use this yet, many changes will be made and a prebuilt binary will be made available as soon as it is ready.

# Usage
Place either `gmsv_gwsockets_win32.dll` (Windows) or `gmsv_gwsockets_linux.dll` (Linux) into you `GarrysMod/lua/bin` folder.

You will also need to require the module in lua before you will be able to use it. You can do this using 

```LUA 
require("gwsockets")
```

# Documentation

### Connecting to a socket server
* First initialize a websocket instance using

  *NOTE:* URL's must include the scheme ( Either `ws://` or `wss://` )

  `Example: "wss://example.com:9999/api/socketserver"`

  ```LUA 
  GWSockets.createWebSocket( url )
  ```

* Next add any cookies or headers you would like to send with the initial request (Optional)

  ```LUA
  WEBSOCKET:setHeader( key, value )
  WEBSOCKET:setCookie( key, value )
  ```
  
* Add some callbacks (Optional)

  ```LUA
  function WEBSOCKET:onMessage( msg )  end
  function WEBSOCKET:onError( err ) end 
  function WEBSOCKET:onConnected() end
  ```
  
* Lastly open the connection
  ```LUA
  WEBSOCKET:open()
  ```
  
* Once connected you can send messages using the `write` function
  ```LUA
  WEBSOCKET:write( message )
  ```

* You can close the websocket connection at any time using `close` OR `closeNow`

  ```LUA
  WEBSOCKET:close()
  WEBSOCKET:closeNow()
  ```

  * `close` will wait for all queued messages to be sent and then gracefully close the connection
  * `closeNow` will immediately terminate the connection and discard all queued messages
  
  
  
## Example:
```LUA
require("gwsockets")

local socket = GWSockets.createWebSocket( "wss://www.example.com:8800/playerConnectionNotifier" )

function socket:onMessage( message ) 
  print( "Recevied: ", message ) 
end

function socket:onError( err ) 
  print( err ) 
end

function socket:onConnected() 
  print( "Connected to websocket server" ) 
end

socket:open()

-- Notify the websocket server everytime a player joins
hook.Add( "PlayerInitialSpawn", "NotifyWebsocketServer", function( ply )
  socket:write( ply:SteamID64() ) 
end )
```

# Build
Requires premake5.
Run BuildProjects.sh or BuildProjects.bat while having premake5 in your PATH.
Then use the appropriate generated solution for your system and build the project.

### Windows
On Windows all you need to do is open the generated visual studio project and build the dll. All libraries and headers are provided already.

### Linux
The required static libraries for linux are not included in this repository because they are usually very easy to obtain from the package manager of your distro. For example on ubuntu all you need to install is:
```console
sudo apt-get install build-essential gcc-multilib g++-multilib
sudo apt-get install libssl-dev:i386 libboost-system-dev:i386
```
Then running the makefile should find all the required static libraries automatically.

