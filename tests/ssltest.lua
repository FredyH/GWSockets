-- This test checks if the module rejects invalid certificates, while allowing valid ones

require("gwsockets")
print("Starting WebSocket SSLTest. This test will take 20 seconds.")
-- Currently no check for revoked certificates implemented
local badURLs = {
	"https://expired.badssl.com/",
	"https://wrong.host.badssl.com/",
	"https://self-signed.badssl.com/",
	"https://untrusted-root.badssl.com/",
	-- "https://revoked.badssl.com/",
	-- "https://invalid-expected-sct.badssl.com/", transparency check not implemented yet
	"https://dh512.badssl.com/",
	"https://subdomain.preloaded-hsts.badssl.com/",
	"https://superfish.badssl.com/";
	"https://edellroot.badssl.com/",
	"https://preact-cli.badssl.com/",
	"https://webpack-dev-server.badssl.com/",
	"https://captive-portal.badssl.com/",
	"https://mitm-software.badssl.com/",
}
local errored = {}
for k,v in pairs(badURLs) do
	local socket = GWSockets.createWebSocket(v)
	function socket:onError(txt)
		if (!txt:find("upgrade handshake failed")) then
			print(v .. " failed as expected: " .. txt)
			errored[k] = true
		else
			print(v .. " connected invalid certificate")
		end
	end
	function socket:onConnected()
		print(v .. " connected invalid certificate")
		self:close()
	end
	socket:open()
end
timer.Simple(10, function()
	for k,v in pairs(badURLs) do
		if (!errored[k]) then
			print("WebSocket SSLTest failed because " .. v .. " did not error")
			return
		end
	end

	local goodURLs = {
		"wss://echo.websocket.events"
	}
	local connected = {}
	for k,v in pairs(goodURLs) do
		local socket = GWSockets.createWebSocket(v)
		function socket:onError(txt)
			print("Error connecting to URL with valid SSL certificate " .. v .. "\n error:" .. txt)
		end
		function socket:onConnected()
			print("Connected to " .. v .. " successfully")
			connected[k] = true
			self:close()
		end
		socket:open()
	end
	timer.Simple(10, function()
		for k,v in pairs(goodURLs) do
			if (!connected[k]) then
				print("WebSocket SSLTest failed because it failed to connect to host with good SSL certificate: " .. v)
				return
			end
		end
		print("WebSocket SSLTest completed without errors.")
	end)
end)