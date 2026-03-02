import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/code-generation.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';

describe('code-generation tools', () => {
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

  describe('generate_code (TCP)', () => {
    it('sends code generation request and returns result on success', async () => {
      mockServer.setResponse(UE_TOOL_TYPES.GENERATE_CODE, {
        type: UE_TOOL_TYPES.GENERATE_CODE,
        success: true,
        data: { filePath: 'Source/MyGame/MyActor.h', action: 'created' },
      });

      const result = await handleToolCall(
        UE_TOOL_TYPES.GENERATE_CODE,
        {
          filePath: 'Source/MyGame/MyActor.h',
          content: '#pragma once\n...',
          description: 'Create new actor header',
        },
        tcpClient,
      );
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('MyActor.h');
      expect(result!.content[0].text).toContain('created');
    });

    it('returns error text for UE non-success response', async () => {
      const mockClientObj = {
        sendRequest: vi.fn().mockResolvedValue({
          success: false,
          error: { code: 403, message: 'User cancelled code generation' },
        }),
      } as unknown as UETCPClient;

      const result = await handleToolCall(
        UE_TOOL_TYPES.GENERATE_CODE,
        { filePath: 'Source/MyActor.h', content: '', description: 'test' },
        mockClientObj,
      );
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('Error');
      expect(result!.content[0].text).toContain('User cancelled');
    });

    it('returns error text when UE is disconnected', async () => {
      tcpClient.disconnect();
      await mockServer.stop();

      const result = await handleToolCall(
        UE_TOOL_TYPES.GENERATE_CODE,
        { filePath: 'Source/MyActor.h', content: '', description: 'test' },
        tcpClient,
      );
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
    });
  });

  describe('get_code_template (local)', () => {
    const templateExpectations: [string, string][] = [
      ['Actor', 'AMyActor'],
      ['Component', 'UMyActorComponent'],
      ['GameMode', 'AMyGameMode'],
      ['Widget', 'UMyUserWidget'],
      ['AnimInstance', 'UMyAnimInstance'],
      ['Interface', 'IMyInterface'],
      ['Struct', 'FMyData'],
      ['Enum', 'EMyEnum'],
    ];

    it.each(templateExpectations)(
      'returns %s template with correct class name',
      async (classType, expectedClass) => {
        const result = await handleToolCall('get_code_template', { classType }, tcpClient);
        expect(result).not.toBeNull();
        expect(result!.content[0].text).toContain(expectedClass);
        expect(result!.content[0].text).toContain('#pragma once');
      },
    );

    it('returns GENERATED_BODY() in Actor template', async () => {
      const result = await handleToolCall('get_code_template', { classType: 'Actor' }, tcpClient);
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('GENERATED_BODY()');
      expect(result!.content[0].text).toContain('UCLASS()');
      expect(result!.content[0].text).toContain('BeginPlay');
      expect(result!.content[0].text).toContain('Tick');
    });

    it('returns error for invalid classType', async () => {
      const result = await handleToolCall('get_code_template', { classType: 'InvalidType' }, tcpClient);
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('Error');
      expect(result!.content[0].text).toContain('classType must be one of');
    });

    it('returns error for missing classType', async () => {
      const result = await handleToolCall('get_code_template', {}, tcpClient);
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('Error');
    });

    it('returns error for non-string classType', async () => {
      const result = await handleToolCall('get_code_template', { classType: 42 }, tcpClient);
      expect(result).not.toBeNull();
      expect(result!.content[0].text).toContain('Error');
    });
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
