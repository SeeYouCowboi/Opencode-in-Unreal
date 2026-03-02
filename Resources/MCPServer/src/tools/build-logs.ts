import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

// ─── Tool Definitions ─────────────────────────────────────────────────

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GET_BUILD_LOGS,
    description:
      'Get structured compilation errors and warnings from the last UE build',
    inputSchema: {
      type: 'object' as const,
      properties: {
        severity: {
          type: 'string',
          enum: ['error', 'warning', 'note', 'all'],
          description:
            'Filter by severity level (default: all)',
        },
        limit: {
          type: 'number',
          description:
            'Maximum number of log entries to return (default: 100)',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_OUTPUT_LOG,
    description:
      'Get recent Unreal Engine Output Log entries, optionally filtered by category',
    inputSchema: {
      type: 'object' as const,
      properties: {
        category: {
          type: 'string',
          description:
            'Filter by log category (e.g. "LogTemp", "LogBlueprintUserMessages")',
        },
        limit: {
          type: 'number',
          description:
            'Maximum number of log entries to return (default: 100)',
        },
        since: {
          type: 'string',
          description:
            'Return only entries after this ISO 8601 timestamp',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_COMPILATION_STATUS,
    description:
      'Get current UE build/compilation status including error and warning counts',
    inputSchema: {
      type: 'object' as const,
      properties: {},
      required: [],
    },
  },
];

// ─── Tool Call Router ─────────────────────────────────────────────────

type ToolResult = { content: Array<{ type: 'text'; text: string }> };

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient,
): Promise<ToolResult | null> {
  switch (name) {
    case UE_TOOL_TYPES.GET_BUILD_LOGS:
    case UE_TOOL_TYPES.GET_OUTPUT_LOG:
    case UE_TOOL_TYPES.GET_COMPILATION_STATUS: {
      try {
        const response = await tcpClient.sendRequest(name, args);
        if (!response.success) {
          return {
            content: [
              {
                type: 'text' as const,
                text: `Error: ${response.error?.message ?? 'Unknown UE error'}`,
              },
            ],
          };
        }
        return {
          content: [
            {
              type: 'text' as const,
              text: JSON.stringify(response.data, null, 2),
            },
          ],
        };
      } catch (err) {
        return {
          content: [
            {
              type: 'text' as const,
              text: `UE not connected: ${err}`,
            },
          ],
        };
      }
    }
    default:
      return null;
  }
}
