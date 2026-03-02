import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/blueprint-assets.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';
import { sampleBlueprintList } from '../helpers/fixtures.js';

describe('blueprint-assets tools', () => {
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

  it('get_blueprint_list returns blueprints on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_BLUEPRINT_LIST, {
      type: UE_TOOL_TYPES.GET_BLUEPRINT_LIST,
      success: true,
      data: sampleBlueprintList,
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_BLUEPRINT_LIST, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('BP_PlayerCharacter');
    expect(result!.content[0].text).toContain('WBP_HUD');
  });

  it('get_blueprint_details returns data on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS, {
      type: UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS,
      success: true,
      data: { name: 'BP_Player', variables: ['Health', 'Stamina'], functions: ['OnDamaged'] },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS,
      { blueprintPath: '/Game/Blueprints/BP_Player' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('BP_Player');
    expect(result!.content[0].text).toContain('Health');
  });

  it('search_blueprints returns results on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.SEARCH_BLUEPRINTS, {
      type: UE_TOOL_TYPES.SEARCH_BLUEPRINTS,
      success: true,
      data: { results: [{ name: 'BP_Enemy', path: '/Game/Blueprints/BP_Enemy' }] },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.SEARCH_BLUEPRINTS,
      { query: 'Enemy' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('BP_Enemy');
  });

  it('returns error text for UE non-success response', async () => {
    const mockClientObj = {
      sendRequest: vi.fn().mockResolvedValue({
        success: false,
        error: { code: 404, message: 'Blueprint not found' },
      }),
    } as unknown as UETCPClient;

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS,
      { blueprintPath: '/Game/Missing' },
      mockClientObj,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Error');
    expect(result!.content[0].text).toContain('Blueprint not found');
  });

  it('returns error text when UE is disconnected', async () => {
    tcpClient.disconnect();
    await mockServer.stop();

    const result = await handleToolCall(UE_TOOL_TYPES.GET_BLUEPRINT_LIST, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
