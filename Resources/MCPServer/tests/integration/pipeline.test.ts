import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import * as net from 'net';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { UETCPClient } from '../../src/tcp-client.js';

function restoreEnv(savedEnv: Record<string, string | undefined>): void {
  for (const [key, value] of Object.entries(savedEnv)) {
    if (value === undefined) {
      delete process.env[key];
      continue;
    }
    process.env[key] = value;
  }
}

async function waitForConnected(client: UETCPClient, timeoutMs: number): Promise<void> {
  const startedAt = Date.now();
  while (!client.isConnected()) {
    if (Date.now() - startedAt >= timeoutMs) {
      throw new Error(`Client did not reconnect within ${timeoutMs}ms`);
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
}

describe('TCP integration pipeline scenarios', () => {
  let mockServer: MockTCPServer;
  let client: UETCPClient;
  let reconnectServer: MockTCPServer | null = null;
  const savedEnv: Record<string, string | undefined> = {};

  beforeEach(async () => {
    savedEnv.UEOC_HOST = process.env.UEOC_HOST;
    savedEnv.UEOC_PORT = process.env.UEOC_PORT;
    savedEnv.UEOC_REQUEST_TIMEOUT = process.env.UEOC_REQUEST_TIMEOUT;
    savedEnv.UEOC_RECONNECT_ATTEMPTS = process.env.UEOC_RECONNECT_ATTEMPTS;
    savedEnv.UEOC_RECONNECT_INTERVAL = process.env.UEOC_RECONNECT_INTERVAL;

    mockServer = new MockTCPServer();
    await mockServer.start();

    process.env.UEOC_HOST = '127.0.0.1';
    process.env.UEOC_PORT = String(mockServer.port);
    process.env.UEOC_REQUEST_TIMEOUT = '1000';
    process.env.UEOC_RECONNECT_ATTEMPTS = '0';
    process.env.UEOC_RECONNECT_INTERVAL = '100';

    client = new UETCPClient();
    await client.connect();
  });

  afterEach(async () => {
    client.disconnect();

    try {
      await mockServer.stop();
    } catch {
    }

    if (reconnectServer) {
      try {
        await reconnectServer.stop();
      } catch {
      }
      reconnectServer = null;
    }

    restoreEnv(savedEnv);
  });

  it('returns a clear error when UE is disconnected during a tool call', async () => {
    client.disconnect();

    await expect(client.sendRequest('get_project_structure', {})).rejects.toThrow(
      /Not connected to Unreal Engine editor/,
    );
  });

  it('returns timeout error when UE does not respond within 200ms', async () => {
    client.disconnect();
    try {
      await mockServer.stop();
    } catch {
    }

    const silentSockets: net.Socket[] = [];
    const silentServer = net.createServer((socket) => {
      silentSockets.push(socket);
    });

    await new Promise<void>((resolve) => silentServer.listen(0, '127.0.0.1', () => resolve()));
    const silentPort = (silentServer.address() as net.AddressInfo).port;

    process.env.UEOC_PORT = String(silentPort);
    process.env.UEOC_REQUEST_TIMEOUT = '200';
    process.env.UEOC_RECONNECT_ATTEMPTS = '0';

    client = new UETCPClient();
    await client.connect();

    await expect(client.sendRequest('get_cpp_hierarchy', {})).rejects.toThrow(/timeout/i);

    client.disconnect();
    for (const socket of silentSockets) {
      socket.destroy();
    }
    await new Promise<void>((resolve) => silentServer.close(() => resolve()));
  });

  it('returns Unknown tool error for malformed or unknown tool calls', async () => {
    mockServer.setResponse('unknown_tool', {
      type: 'unknown_tool',
      success: false,
      error: { code: 400, message: 'Unknown tool: unknown_tool' },
    });

    await expect(client.sendRequest('unknown_tool', {})).rejects.toThrow(/Unknown tool/);
  });

  it('handles five concurrent tool calls successfully', async () => {
    for (let index = 1; index <= 5; index++) {
      const type = `tool_${index}`;
      mockServer.setResponse(type, {
        type,
        success: true,
        data: { index, ok: true },
      });
    }

    const responses = await Promise.all([
      client.sendRequest('tool_1', {}),
      client.sendRequest('tool_2', {}),
      client.sendRequest('tool_3', {}),
      client.sendRequest('tool_4', {}),
      client.sendRequest('tool_5', {}),
    ]);

    expect(responses).toHaveLength(5);
    expect(responses[0].data).toEqual({ index: 1, ok: true });
    expect(responses[1].data).toEqual({ index: 2, ok: true });
    expect(responses[2].data).toEqual({ index: 3, ok: true });
    expect(responses[3].data).toEqual({ index: 4, ok: true });
    expect(responses[4].data).toEqual({ index: 5, ok: true });
  });

  it('reconnects and resumes operations after server restart', async () => {
    client.disconnect();
    process.env.UEOC_RECONNECT_ATTEMPTS = '3';
    process.env.UEOC_RECONNECT_INTERVAL = '100';

    client = new UETCPClient();
    await client.connect();

    const reconnectPort = mockServer.port;
    await mockServer.stop();

    reconnectServer = new MockTCPServer(reconnectPort);
    await reconnectServer.start();
    reconnectServer.setResponse('after_restart', {
      type: 'after_restart',
      success: true,
      data: { stage: 'after' },
    });

    await waitForConnected(client, 2000);

    const startedAt = Date.now();
    let afterRestart: Awaited<ReturnType<UETCPClient['sendRequest']>> | null = null;
    while (!afterRestart && Date.now() - startedAt < 2000) {
      try {
        afterRestart = await client.sendRequest('after_restart', {});
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
    }

    expect(afterRestart).not.toBeNull();
    if (!afterRestart) {
      throw new Error('Failed to send request after reconnect');
    }
    expect(afterRestart.data).toEqual({ stage: 'after' });
  });
});
