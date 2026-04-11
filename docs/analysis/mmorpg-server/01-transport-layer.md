# 1. Transport Layer — uWebSockets.js Adapter

**File:** `src/utils/uws-adapter.ts`

## Overview

Uses [uWebSockets.js](https://github.com/uNetworking/uWebSockets.js) — a C++ WebSocket server with Node.js bindings. Significantly faster than `ws` (the standard Node.js WebSocket library).

## Architecture

```
NestJS WebSocketGateway
    │
    ▼
UWebSocketAdapter (implements WebSocketAdapter)
    │
    ▼
uWebSockets.js (C++ engine, non-blocking)
    │
    ▼
TCP + WebSocket framing
```

## Key Implementation

```typescript
bindClientConnect(server, callback) {
    this.instance.ws('/*', {
        open: (socket) => {
            // Attach EventEmitter to socket for NestJS integration
            Object.defineProperty(socket, 'emitter', {
                value: new events.EventEmitter(),
            });
            
            // Override send to handle Uint8Array → ArrayBuffer conversion
            const originalSend = socket.send.bind(socket);
            socket.send = (data, isBinary = true, compress = false) => {
                if (typeof data === 'string')
                    return originalSend(data, isBinary, compress);
                else {
                    const arrayBuffer = data.buffer.slice(
                        data.byteOffset, data.byteOffset + data.byteLength);
                    return originalSend(arrayBuffer, isBinary, compress);
                }
            };
            callback(socket);
        },
        message: (socket, message, isBinary) => {
            socket['emitter'].emit('message', { message, isBinary });
        },
    });
}
```

## Message Routing

```typescript
bindMessageHandler(bufferArr, handlers, process, diffPublicKey) {
    const buffer = Buffer.from(bufferArr.message);
    const pBuffer = ByteBuffer.toArrayBuffer(buffer);
    const encryptPacket = pBuffer[0];  // First byte: 0 = plain, 1 = encrypted

    if (diffPublicKey && encryptPacket === 1) {
        // Decrypt with XOR
        const dataBuffer = ByteBuffer.toArrayBuffer(buffer, 1);
        const message = new ByteBuffer(this.decryptMessage(dataBuffer, diffPublicKey));
        const type = message.getByte();

        if (type === ClientPacketType.Queue) {
            // Batched packet — split and process each sub-packet
            const packets = ByteBuffer.splitPackets(message);
            for (let packet of packets) {
                const subType = packet.getByte();
                // Route to handler...
            }
        } else {
            // Single packet — route directly
        }
    } else if (encryptPacket === 0) {
        // Unencrypted — only allow auth-related packets
        // (Ping, Login, CharacterList, EnterToWorld, CreateCharacter)
    }
}
```

## SSL Support

```typescript
static buildSSLApp(params) {
    return UWS.SSLApp({
        cert_file_name: params.sslCert,
        key_file_name: params.sslKey,
    });
}
```

Can run as WSS (WebSocket Secure) with TLS certificate.

## Gateway

```typescript
@WebSocketGateway(3011, { cors: { origin: "*" }})
export class GameServerGateway implements OnGatewayInit, OnGatewayConnection, OnGatewayDisconnect {
    private clients: Map<string, any> = new Map();
    // Redis for pub/sub between server instances
    // Bull queue for async tasks
}
```

Port 3011, CORS enabled for browser clients. Uses Redis for inter-server communication and Bull for job queues.
