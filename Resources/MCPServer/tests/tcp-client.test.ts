import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import * as net from 'net';
import { MockTCPServer } from './helpers/mock-tcp-server.js';
import { UETCPClient } from '../src/tcp-client.js';

describe('UETCPClient', () => {
  let mockServer: MockTCPServer;
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
    process.env.UEOC_RECONNECT_ATTEMPTS = '0';
    process.env.UEOC_RECONNECT_INTERVAL = '100';
  });

  afterEach(async () => {
    try { await mockServer.stop(); } catch { /* may already be stopped */ }
    for (const [key, val] of Object.entries(savedEnv)) {
      if (val === undefined) delete process.env[key];
      else process.env[key] = val;
    }
  });

  it('connects to mock server and reports connected', async () => {
    const client = new UETCPClient();
    await client.connect();
    expect(client.isConnected()).toBe(true);
    client.disconnect();
    expect(client.isConnected()).toBe(false);
  });

  it('sends request and receives matching response', async () => {
    const client = new UETCPClient();
    await client.connect();

    mockServer.setResponse('test_type', {
      type: 'test_type',
      success: true,
      data: { hello: 'world' },
    });

    const response = await client.sendRequest('test_type', { foo: 'bar' });
    expect(response.success).toBe(true);
    expect(response.data).toEqual({ hello: 'world' });
    expect(response.type).toBe('test_type');

    // Verify the server received the request with correct params
    expect(mockServer.receivedMessages.length).toBe(1);
    const received = mockServer.receivedMessages[0] as Record<string, unknown>;
    expect(received.type).toBe('test_type');
    expect((received.params as Record<string, unknown>).foo).toBe('bar');

    client.disconnect();
  });

  it('handles default mock response when no override is set', async () => {
    const client = new UETCPClient();
    await client.connect();

    const response = await client.sendRequest('any_type', {});
    expect(response.success).toBe(true);
    expect(response.data).toEqual({ message: 'mock response' });

    client.disconnect();
  });

  it('throws when sending request while disconnected', async () => {
    const client = new UETCPClient();
    await client.connect();
    client.disconnect();

    await expect(client.sendRequest('test', {})).rejects.toThrow(/Not connected/);
  });

  it('rejects pending requests on disconnect', async () => {
    // Use a silent server so requests stay pending
    const silentSockets: net.Socket[] = [];
    const silentServer = net.createServer((socket) => { silentSockets.push(socket); });
    await new Promise<void>((resolve) => silentServer.listen(0, '127.0.0.1', () => resolve()));
    const silentPort = (silentServer.address() as net.AddressInfo).port;

    process.env.UEOC_PORT = String(silentPort);
    process.env.UEOC_REQUEST_TIMEOUT = '5000';

    const client = new UETCPClient();
    await client.connect();

    const requestPromise = client.sendRequest('test', {});
    client.disconnect();

    await expect(requestPromise).rejects.toThrow(/disconnected/i);

    for (const s of silentSockets) s.destroy();
    await new Promise<void>((resolve) => silentServer.close(() => resolve()));
  });

  it('times out when server does not respond', async () => {
    const silentSockets: net.Socket[] = [];
    const silentServer = net.createServer((socket) => { silentSockets.push(socket); });
    await new Promise<void>((resolve) => silentServer.listen(0, '127.0.0.1', () => resolve()));
    const silentPort = (silentServer.address() as net.AddressInfo).port;

    process.env.UEOC_PORT = String(silentPort);
    process.env.UEOC_REQUEST_TIMEOUT = '200';

    const client = new UETCPClient();
    await client.connect();

    await expect(client.sendRequest('test_type', {})).rejects.toThrow(/timeout/i);

    client.disconnect();
    for (const s of silentSockets) s.destroy();
    await new Promise<void>((resolve) => silentServer.close(() => resolve()));
  });

  it('rejects with error message when server returns error response', async () => {
    const client = new UETCPClient();
    await client.connect();

    mockServer.setResponse('error_type', {
      type: 'error_type',
      success: false,
      error: { code: 500, message: 'Server error occurred' },
    });

    await expect(client.sendRequest('error_type', {})).rejects.toThrow(/Server error occurred/);

    client.disconnect();
  });

  it('handles multiple concurrent requests', async () => {
    const client = new UETCPClient();
    await client.connect();

    mockServer.setResponse('type_a', {
      type: 'type_a',
      success: true,
      data: { result: 'a' },
    });
    mockServer.setResponse('type_b', {
      type: 'type_b',
      success: true,
      data: { result: 'b' },
    });

    const [responseA, responseB] = await Promise.all([
      client.sendRequest('type_a', {}),
      client.sendRequest('type_b', {}),
    ]);

    expect(responseA.data).toEqual({ result: 'a' });
    expect(responseB.data).toEqual({ result: 'b' });

    client.disconnect();
  });
});
