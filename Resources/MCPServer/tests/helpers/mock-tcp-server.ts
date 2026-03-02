import * as net from 'net';

export interface MockResponse {
  type: string;
  success: boolean;
  data?: unknown;
  error?: { code: number; message: string };
}

export class MockTCPServer {
  private server: net.Server;
  private clients: net.Socket[] = [];
  public port: number;
  public receivedMessages: unknown[] = [];
  private responseOverrides: Map<string, MockResponse> = new Map();

  constructor(port = 0) { // port=0 means OS assigns a free port
    this.port = port;
    this.server = net.createServer();
  }

  /** Set a custom response for a specific request type */
  setResponse(type: string, response: MockResponse): void {
    this.responseOverrides.set(type, response);
  }

  async start(): Promise<number> {
    return new Promise((resolve, reject) => {
      this.server.listen(this.port, '127.0.0.1', () => {
        const addr = this.server.address() as net.AddressInfo;
        this.port = addr.port;
        resolve(this.port);
      });
      this.server.on('error', reject);
      this.server.on('connection', (socket) => {
        this.clients.push(socket);
        this.handleClient(socket);
      });
    });
  }

  private handleClient(socket: net.Socket): void {
    let buffer = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      // Parse length-prefixed messages (4-byte big-endian uint32 + JSON)
      while (buffer.length >= 4) {
        const msgLen = buffer.readUInt32BE(0);
        if (buffer.length < 4 + msgLen) break;
        const msgJson = buffer.slice(4, 4 + msgLen).toString('utf8');
        buffer = buffer.slice(4 + msgLen);
        const request = JSON.parse(msgJson);
        this.receivedMessages.push(request);
        // Build response
        const override = this.responseOverrides.get(request.type);
        const response = {
          id: request.id,
          type: request.type,
          success: override ? override.success : true,
          data: override ? override.data : { message: 'mock response' },
          error: override?.error,
          timestamp: Date.now(),
        };
        const responseJson = JSON.stringify(response);
        const responseBuffer = Buffer.from(responseJson, 'utf8');
        const header = Buffer.alloc(4);
        header.writeUInt32BE(responseBuffer.length, 0);
        socket.write(Buffer.concat([header, responseBuffer]));
      }
    });
  }

  async stop(): Promise<void> {
    // Destroy all client sockets first (otherwise server.close() waits forever)
    for (const client of this.clients) {
      client.destroy();
    }
    this.clients = [];
    return new Promise((resolve) => this.server.close(() => resolve()));
  }

  reset(): void {
    this.receivedMessages = [];
    this.responseOverrides.clear();
  }
}
