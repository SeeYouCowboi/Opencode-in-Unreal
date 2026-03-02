import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { UETCPClient } from './tcp-client.js';
import { registerTools } from './tools/index.js';

export class UnrealOpenCodeMCPServer {
  private server: Server;
  private tcpClient: UETCPClient;

  constructor() {
    this.server = new Server(
      { name: 'unrealopencode', version: '0.1.0' },
      { capabilities: { tools: {} } }
    );
    this.tcpClient = new UETCPClient();
  }

  async start(): Promise<void> {
    // Connect TCP client to UE Plugin (non-fatal if UE not running)
    try {
      await this.tcpClient.connect();
    } catch (err) {
      // UE plugin may not be running — tools will return appropriate errors
      process.stderr.write(`[UnrealOpenCode] UE plugin not connected: ${err}\n`);
    }

    registerTools(this.server, this.tcpClient);

    const transport = new StdioServerTransport();
    await this.server.connect(transport);
    process.stderr.write('[UnrealOpenCode] MCP Server started via stdio\n');
  }

  async stop(): Promise<void> {
    this.tcpClient.disconnect();
    await this.server.close();
  }
}
