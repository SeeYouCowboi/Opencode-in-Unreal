import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/cpp-hierarchy.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';
import { sampleCppHierarchy } from '../helpers/fixtures.js';

describe('cpp-hierarchy tools', () => {
  let mockServer: MockTCPServer;
  let tcpClient: UETCPClient;
  const savedEnv: Record<string, string | undefined> = {};

  beforeEach(async () => {
    savedEnv.UEOC_HOST = process.env.UEOC_HOST;
    savedEnv.UEOC_PORT = process.env.UEOC_PORT;
    savedEnv.UEOC_RECONNECT_ATTEMPTS = process.env.UEOC_RECONNECT_ATTEMPTS;

    mockServer = new MockTCPServer();
    await mockServer.start();

    process.env.UEOC_HOST = '127.0.0.1';
    process.env.UEOC_PORT = String(mockServer.port);
    process.env.UEOC_RECONNECT_ATTEMPTS = '0';

    tcpClient = new UETCPClient();
    await tcpClient.connect();
  });

  afterEach(async () => {
    tcpClient.disconnect();
    try { await mockServer.stop(); } catch { /* may already be stopped */ }
    for (const [key, val] of Object.entries(savedEnv)) {
      if (val === undefined) delete process.env[key];
      else process.env[key] = val;
    }
  });

  it('get_cpp_hierarchy returns class data on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_CPP_HIERARCHY, {
      type: UE_TOOL_TYPES.GET_CPP_HIERARCHY,
      success: true,
      data: sampleCppHierarchy,
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_CPP_HIERARCHY, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('AMyGameCharacter');
    expect(result!.content[0].text).toContain('ACharacter');
  });

  it('get_class_details returns data on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_CLASS_DETAILS, {
      type: UE_TOOL_TYPES.GET_CLASS_DETAILS,
      success: true,
      data: { className: 'AMyActor', properties: ['Health'], functions: ['TakeDamage'] },
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_CLASS_DETAILS, { className: 'AMyActor' }, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('AMyActor');
    expect(result!.content[0].text).toContain('Health');
  });

  it('search_classes returns search results on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.SEARCH_CLASSES, {
      type: UE_TOOL_TYPES.SEARCH_CLASSES,
      success: true,
      data: { results: [{ name: 'AMyGameCharacter' }, { name: 'AMyGamePlayerController' }] },
    });

    const result = await handleToolCall(UE_TOOL_TYPES.SEARCH_CLASSES, { query: 'MyGame' }, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('AMyGameCharacter');
    expect(result!.content[0].text).toContain('AMyGamePlayerController');
  });

  it('returns error text for UE non-success response', async () => {
    const mockClientObj = {
      sendRequest: vi.fn().mockResolvedValue({
        success: false,
        error: { code: 500, message: 'Internal UE error' },
      }),
    } as unknown as UETCPClient;

    const result = await handleToolCall(UE_TOOL_TYPES.GET_CPP_HIERARCHY, {}, mockClientObj);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Error');
    expect(result!.content[0].text).toContain('Internal UE error');
  });

  it('returns error text when UE is disconnected', async () => {
    tcpClient.disconnect();
    await mockServer.stop();

    const result = await handleToolCall(UE_TOOL_TYPES.GET_CPP_HIERARCHY, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
