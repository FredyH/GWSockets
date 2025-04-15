# GWSockets

WebSockets for GLua

## Usage

Place either `gmsv_gwsockets_win32.dll` (Windows) or `gmsv_gwsockets_linux.dll` (Linux) into your `GarrysMod/lua/bin` folder. On windows you will require the Visual C++ Redistributable 2017, which you can find [here](https://support.microsoft.com/help/2977003/the-latest-supported-visual-c-downloads).

*NOTE:* CentOS is currently not supported and appears to be having multiple issues. If you need the library to work on CentOS, compile it on CentOS using the instructions all the way at the bottom, but also replace the included ssl libraries with the ones provided by CentOS.

*NOTE:* Even though this module is mainly aimed at servers, it can also be used on clients. Just rename the module to `gmcl_gwsockets_os` and it will work on clientside as well.

You will also need to require the module in lua before you will be able to use it. You can do this running 

```LUA 
require("gwsockets")
```

## Documentation

### Connecting to a socket server

* First initialize a websocket instance using

  *NOTE:* URL's must include the scheme ( Either `ws://` or `wss://` )

  `Example: "wss://echo.websocket.events/api/socketserver"`

  ```LUA
  GWSockets.createWebSocket( url, verifyCertificate=true )
  ```

  *NOTE:* If you want your websockets to use SSL but don't have a trusted certificate, you can set the second parameter to false.

* If you are running certain versions of Linux (e.g. CentOS) it might be necessary to specify a different path for the root certificates. This is only required if you want to use SSL and verify set verifyCertificates to true when creating a websocket.
 ```LUA
  GWSockets.addVerifyPath( "/etc/ssl/certs" )
  ```

* If you would like to enable the `permessage-deflate` extension which allows you to send and receive compressed messages, you can enable it with the following functions:

  ```LUA
  -- Do note this will only be enabled if the websocket server supports permessage-deflate and enables it during handshake.
  WEBSOCKET:setMessageCompression(true)
  ```

* You can also disable context takeover during compression, which will prevent re-using the same compression context over multiple messages.
  This will decrease the memory usage at the cost of a worse compression ratio.

  ```LUA
  WEBSOCKET:setDisableContextTakeover(true)
  ```

 *WARNING:* Enabling compression over encrypted connections (`WSS://`) may make you vulnerable to [CRIME](https://en.wikipedia.org/wiki/CRIME)/[BREACH](https://en.wikipedia.org/wiki/BREACH) attacks.
            Make sure you know what you are doing, or avoid sending sensitive information over websocket messages.

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
  WEBSOCKET:open( shouldClearQueue = true )
  ```
  
  *NOTE:* By default, opening a connection will clear the queued messages. This is due to the fact there
  is no way of knowing what's in the queue, and what has been received by the remote. If you would like to
  disable this, you may use `open(false)`.
  
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
```

## Build

Requires [premake5](https://premake.github.io/download.html#v5).

Depending on your platform run one of the following commands to create a build script:
```console
premake5 --os=windows --file=BuildProjects.lua vs2010    # Windows
premake5 --os=linux   --file=BuildProjects.lua gmake     # Linux
premake5 --os=macosx  --file=BuildProjects.lua gmake     # Mac
```
Then use the appropriate generated solution for your system in the solutions/ folder and build the project.

### Windows

On Windows all you need to do is open the generated visual studio project and build the dll. All libraries and headers are provided already. If you wish to build the 64 bit version you just have to switch the build configuration to x64.

### Linux

On linux only essential programs for building C++ programs are required. On Ubuntu 64-bit these are:

```console
sudo apt-get install build-essential gcc-multilib g++-multilib
```

The required static libraries for linux are included in this repository to avoid library/header version mismatching, but feel free to use your OS' libraries instead.

To build the project simply run
```console
make                           # x86
make config=release_x86_64     # x64
```

### Possible Issues

This library uses OpenSSL built for Ubuntu, which sets the default search path for
root certificates to the one Ubuntu uses. There is a possibility, that this path
is different on other systems. In that case you will need to swap out the libssl.a
and libcrypto.a provided in this repositor with the ones provided by your
operating system.
