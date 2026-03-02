import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

type ToolCallResult = {
  content: Array<{ type: 'text'; text: string }>;
};

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GET_BLUEPRINT_LIST,
    description:
      'Lists all Blueprint assets in the project, optionally filtered by a content path prefix.',
    inputSchema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Optional content path prefix filter, e.g. /Game/Blueprints',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS,
    description:
      'Returns detailed Blueprint information including variables, functions, parent class, interfaces, and graph summary.',
    inputSchema: {
      type: 'object',
      properties: {
        blueprintPath: {
          type: 'string',
          description: 'Blueprint asset path, e.g. /Game/Blueprints/BP_Player',
        },
      },
      required: ['blueprintPath'],
    },
  },
  {
    name: UE_TOOL_TYPES.SEARCH_BLUEPRINTS,
    description:
      'Searches Blueprint assets by partial name or class, with optional parent class filtering. Returns top 20 matches.',
    inputSchema: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'Partial Blueprint name or class query.',
        },
        parentClass: {
          type: 'string',
          description: 'Optional parent class name filter.',
        },
      },
      required: ['query'],
    },
  },
];

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient,
): Promise<ToolCallResult | null> {
  switch (name) {
    case UE_TOOL_TYPES.GET_BLUEPRINT_LIST:
    case UE_TOOL_TYPES.GET_BLUEPRINT_DETAILS:
    case UE_TOOL_TYPES.SEARCH_BLUEPRINTS: {
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
          content: [{ type: 'text' as const, text: JSON.stringify(response.data, null, 2) }],
        };
      } catch (err) {
        return {
          content: [
            {
              type: 'text' as const,
              text: `UE not connected: ${String(err)}. Open the UE Editor with UnrealOpenCode plugin enabled.`,
            },
          ],
        };
      }
    }
    default:
      return null;
  }
}
