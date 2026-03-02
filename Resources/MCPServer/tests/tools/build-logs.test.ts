import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { MockTCPServer } from '../helpers/mock-tcp-server.js';
import { handleToolCall } from '../../src/tools/build-logs.js';
import { UETCPClient } from '../../src/tcp-client.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';

describe('build-logs tools', () => {
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

  it('get_build_logs returns build log entries on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_BUILD_LOGS, {
      type: UE_TOOL_TYPES.GET_BUILD_LOGS,
      success: true,
      data: {
        entries: [
          { severity: 'error', message: 'undeclared identifier', file: 'MyActor.cpp', line: 42 },
          { severity: 'warning', message: 'unused variable', file: 'MyComponent.cpp', line: 10 },
        ],
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_BUILD_LOGS,
      { severity: 'all', limit: 50 },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('undeclared identifier');
    expect(result!.content[0].text).toContain('unused variable');
  });

  it('get_output_log returns log entries on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_OUTPUT_LOG, {
      type: UE_TOOL_TYPES.GET_OUTPUT_LOG,
      success: true,
      data: {
        entries: [
          { category: 'LogTemp', verbosity: 'Display', message: 'Hello from game' },
          { category: 'LogBlueprintUserMessages', verbosity: 'Warning', message: 'BP warning' },
        ],
      },
    });

    const result = await handleToolCall(
      UE_TOOL_TYPES.GET_OUTPUT_LOG,
      { category: 'LogTemp', limit: 100 },
      tcpClient,
    );
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Hello from game');
  });

  it('get_compilation_status returns build status on success', async () => {
    mockServer.setResponse(UE_TOOL_TYPES.GET_COMPILATION_STATUS, {
      type: UE_TOOL_TYPES.GET_COMPILATION_STATUS,
      success: true,
      data: {
        isCompiling: false,
        lastBuildSucceeded: true,
        errorCount: 0,
        warningCount: 2,
      },
    });

    const result = await handleToolCall(UE_TOOL_TYPES.GET_COMPILATION_STATUS, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('"warningCount": 2');
    expect(result!.content[0].text).toContain('"errorCount": 0');
  });

  it('returns error text for UE non-success response', async () => {
    const mockClientObj = {
      sendRequest: vi.fn().mockResolvedValue({
        success: false,
        error: { code: 503, message: 'Build system unavailable' },
      }),
    } as unknown as UETCPClient;

    const result = await handleToolCall(UE_TOOL_TYPES.GET_BUILD_LOGS, {}, mockClientObj);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toContain('Error');
    expect(result!.content[0].text).toContain('Build system unavailable');
  });

  it('returns error text when UE is disconnected', async () => {
    tcpClient.disconnect();
    await mockServer.stop();

    const result = await handleToolCall(UE_TOOL_TYPES.GET_BUILD_LOGS, {}, tcpClient);
    expect(result).not.toBeNull();
    expect(result!.content[0].text).toMatch(/not connected|error|failed/i);
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, tcpClient);
    expect(result).toBeNull();
  });
});
