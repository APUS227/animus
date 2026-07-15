# Connecting MCP clients to your Animus device

Your device's endpoint is printed on the serial monitor at boot:

```
I (5324) animus:  MCP endpoint : http://192.168.1.50/mcp
```

Replace `192.168.1.50` below with your device's IP. Tip: give the device a
DHCP reservation in your router so the IP never changes.

## 1. Smoke tests with curl

Initialize (note the `Mcp-Session-Id` response header):

```bash
curl -si http://192.168.1.50/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc": "2.0", "id": 1, "method": "initialize",
  "params": {
    "protocolVersion": "2025-11-25",
    "capabilities": {},
    "clientInfo": { "name": "curl", "version": "0" }
  }
}'
```

List tools:

```bash
curl -s http://192.168.1.50/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
```

Switch the demo output ON (watch it auto-off after `max_on_ms`):

```bash
curl -s http://192.168.1.50/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"gpio_set","arguments":{"pin":21,"state":true}}}'
```

Try a pin that is **not** allow-listed and read the refusal — that's the
safety layer talking:

```bash
curl -s http://192.168.1.50/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"gpio_set","arguments":{"pin":13,"state":true}}}'
```

## 2. Claude Code (recommended)

```bash
claude mcp add --transport http animus http://192.168.1.50/mcp
```

If you set a bearer token in `menuconfig`:

```bash
claude mcp add --transport http animus http://192.168.1.50/mcp \
  --header "Authorization: Bearer YOUR_TOKEN"
```

Verify with `claude mcp list`, or `/mcp` inside a session. Then simply ask:

> What tools does my animus device expose? Turn the demo LED on.

## 3. Claude Desktop

Claude Desktop launches local stdio servers, so use the
[`mcp-remote`](https://www.npmjs.com/package/mcp-remote) bridge. In
`claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "animus": {
      "command": "npx",
      "args": ["mcp-remote", "http://192.168.1.50/mcp", "--allow-http"]
    }
  }
}
```

(`--allow-http` is required because the device serves plain HTTP on your LAN.
See the mcp-remote README for header options if you use a bearer token.)

## 4. MCP Inspector

```bash
npx @modelcontextprotocol/inspector
```

Choose transport **Streamable HTTP** and enter `http://192.168.1.50/mcp`.
The Inspector runs on `localhost`, which Animus's Origin check explicitly
allows.

## 5. Claude.ai custom connectors

Claude.ai (web) requires connectors to be reachable over **public HTTPS**, so
a LAN-only device can't be added directly. Options: a reverse proxy/tunnel
with TLS termination, or just use Claude Code / Claude Desktop on the same
network. Native TLS on-device is on the roadmap.

## Troubleshooting

- **403 Forbidden** — your client sent a non-local `Origin` header; that's the
  DNS-rebinding protection working as intended.
- **401 Unauthorized** — a bearer token is set in the firmware; pass the
  matching `Authorization` header.
- **Timeouts** — confirm the computer and the ESP32 are on the same network
  and that your router doesn't isolate WiFi clients ("AP isolation").
