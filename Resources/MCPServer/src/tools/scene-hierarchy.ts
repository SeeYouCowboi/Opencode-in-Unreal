import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

// ─── Tool Definitions ─────────────────────────────────────────────────

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GET_SCENE_HIERARCHY,
    description:
      'Get all actors in the current persistent level as a hierarchy tree with optional component details',
    inputSchema: {
      type: 'object' as const,
      properties: {
        includeComponents: {
          type: 'boolean',
          description:
            'Include component details for each actor (default: true)',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_ACTOR_DETAILS,
    description:
      'Get detailed information for a specific actor including components, properties, tags, and selection state',
    inputSchema: {
      type: 'object' as const,
      properties: {
        actorName: {
          type: 'string',
          description: 'Name of the actor to inspect',
        },
      },
      required: ['actorName'],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_SELECTED_ACTORS,
    description:
      'Get the actors currently selected in the Unreal Engine editor viewport',
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
    case UE_TOOL_TYPES.GET_SCENE_HIERARCHY:
    case UE_TOOL_TYPES.GET_ACTOR_DETAILS:
    case UE_TOOL_TYPES.GET_SELECTED_ACTORS: {
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
              text: `UE not connected: ${err instanceof Error ? err.message : String(err)}`,
            },
          ],
        };
      }
    }
    default:
      return null;
  }
}
