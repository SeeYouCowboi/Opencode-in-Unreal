import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/scene-hierarchy.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';

describe('scene-hierarchy tools', () => {
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

  it('get_scene_hierarchy returns actors on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_SCENE_HIERARCHY, {
      type: UE_TOOL_TYPES.GET_SCENE_HIERARCHY,
      success: true,
      data: {
        actors: [
          { name: 'PlayerStart', class: 'APlayerStart', components: [] },
          { name: 'DirectionalLight', class: 'ADirectionalLight', components: ['LightComponent'] },
        ],
      },
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_SCENE_HIERARCHY, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('PlayerStart');
    expect(result!.content[0].text).toContain('DirectionalLight');
  });

  it('get_actor_details returns detailed actor info on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_ACTOR_DETAILS, {
      type: UE_TOOL_TYPES.GET_ACTOR_DETAILS,
      success: true,
      data: {
        name: 'BP_Enemy_01',
        class: 'AEnemyCharacter',
        components: ['CapsuleComponent', 'SkeletalMeshComponent'],
        tags: ['Enemy', 'Patrol'],
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_ACTOR_DETAILS,
      { actorName: 'BP_Enemy_01' },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('BP_Enemy_01');
    expect(result!.content[0].text).toContain('AEnemyCharacter');
    expect(result!.content[0].text).toContain('Patrol');
  });

  it('get_selected_actors returns selected actors on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_SELECTED_ACTORS, {
      type: UE_TOOL_TYPES.GET_SELECTED_ACTORS,
      success: true,
      data: { selectedActors: [{ name: 'Cube_01', class: 'AStaticMeshActor' }] },
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_SELECTED_ACTORS, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Cube_01');
  });

  it('returns error text for UE non-success response', async () => {
    const mockClientObj = {
      sendRequest: vi.fn().mockResolvedValue({
        success: false,
        error: { code: 404, message: 'Actor not found' },
      }),
    } as unknown as UETCPClient;

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_ACTOR_DETAILS,
      { actorName: 'NonExistent' },
      mockClientObj,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Error');
    expect(result!.content[0].text).toContain('Actor not found');
  });

  it('returns error text when UE is disconnected', async () => {
    tcpClient.disconnect();
    await mockServer.stop();

    const result = await handleToolCall(UE_TOOL_TYPES.GET_SCENE_HIERARCHY, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
