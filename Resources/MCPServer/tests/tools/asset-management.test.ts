import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/asset-management.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';

describe('asset-management tools', () => {
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

  it('search_assets returns matching assets on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.SEARCH_ASSETS, {
      type: UE_TOOL_TYPES.SEARCH_ASSETS,
      success: true,
      data: {
        assets: [
          { name: 'SM_Chair', path: '/Game/Meshes/SM_Chair', type: 'StaticMesh' },
          { name: 'SM_Table', path: '/Game/Meshes/SM_Table', type: 'StaticMesh' },
        ],
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.SEARCH_ASSETS,
      { query: 'SM_', assetType: 'StaticMesh' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('SM_Chair');
    expect(result!.content[0].text).toContain('SM_Table');
  });

  it('get_asset_details returns detailed metadata on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_ASSET_DETAILS, {
      type: UE_TOOL_TYPES.GET_ASSET_DETAILS,
      success: true,
      data: {
        name: 'SM_Chair',
        path: '/Game/Meshes/SM_Chair',
        type: 'StaticMesh',
        triangleCount: 1500,
        lodCount: 3,
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_ASSET_DETAILS,
      { assetPath: '/Game/Meshes/SM_Chair' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('SM_Chair');
    expect(result!.content[0].text).toContain('1500');
  });

  it('get_asset_references returns reference graph on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_ASSET_REFERENCES, {
      type: UE_TOOL_TYPES.GET_ASSET_REFERENCES,
      success: true,
      data: {
        referencers: ['/Game/Maps/MainLevel'],
        dependencies: ['/Game/Textures/T_Wood'],
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_ASSET_REFERENCES,
      { assetPath: '/Game/Materials/M_Wood', direction: 'both' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('MainLevel');
    expect(result!.content[0].text).toContain('T_Wood');
  });

  it('returns error text for UE non-success response', async () => {
    const mockClientObj = {
      sendRequest: vi.fn().mockResolvedValue({
        success: false,
        error: { code: 404, message: 'Asset not found' },
      }),
    } as unknown as UETCPClient;

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_ASSET_DETAILS,
      { assetPath: '/Game/Missing' },
      mockClientObj,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Error');
    expect(result!.content[0].text).toContain('Asset not found');
  });

  it('returns error text when UE is disconnected', async () => {
    tcpClient.disconnect();
    await mockServer.stop();

    const result = await handleToolCall(UE_TOOL_TYPES.SEARCH_ASSETS, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
