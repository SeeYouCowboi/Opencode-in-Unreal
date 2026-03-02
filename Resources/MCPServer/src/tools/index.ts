import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { TOOL_DEFINITIONS as projectDefs, handleToolCall as projectHandler } from './project-structure.js';
import { TOOL_DEFINITIONS as cppDefs, handleToolCall as cppHandler } from './cpp-hierarchy.js';
import { TOOL_DEFINITIONS as bpDefs, handleToolCall as bpHandler } from './blueprint-assets.js';
import { TOOL_DEFINITIONS as sceneDefs, handleToolCall as sceneHandler } from './scene-hierarchy.js';
import { TOOL_DEFINITIONS as assetDefs, handleToolCall as assetHandler } from './asset-management.js';
import { TOOL_DEFINITIONS as buildDefs, handleToolCall as buildHandler } from './build-logs.js';
import { TOOL_DEFINITIONS as codeDefs, handleToolCall as codeHandler } from './code-generation.js';

const ALL_TOOL_DEFINITIONS = [
  ...projectDefs, ...cppDefs, ...bpDefs, ...sceneDefs, ...assetDefs, ...buildDefs, ...codeDefs,
  { name: 'ping', description: 'Ping the UE plugin to check connectivity', inputSchema: { type: 'object' as const, properties: {}, required: [] } },
];

const HANDLERS = [projectHandler, cppHandler, bpHandler, sceneHandler, assetHandler, buildHandler, codeHandler];

export function registerTools(server: Server, tcpClient: UETCPClient): void {
  server.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: ALL_TOOL_DEFINITIONS }));
  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    const typedArgs = (args ?? {}) as Record<string, unknown>;
    for (const handler of HANDLERS) {
      const result = await handler(name, typedArgs, tcpClient);
      if (result !== null) return result;
    }
    if (name === 'ping') {
      try {
        const response = await tcpClient.sendRequest('ping', {});
        return { content: [{ type: 'text' as const, text: `pong: ${JSON.stringify(response.data)}` }] };
      } catch {
        return { content: [{ type: 'text' as const, text: 'pong (UE not connected)' }] };
      }
    }
    throw new Error(`Unknown tool: ${name}`);
  });
}
