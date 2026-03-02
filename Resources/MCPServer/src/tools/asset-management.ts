import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

type ToolCallResult = {
  content: Array<{ type: 'text'; text: string }>;
};

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.SEARCH_ASSETS,
    description:
      'Searches project assets by name, type, or content path. Supports pagination. Returns matching asset paths, types, sizes, and modification timestamps.',
    inputSchema: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'Optional search query to match against asset names.',
        },
        assetType: {
          type: 'string',
          description:
            'Optional asset type filter, e.g. "StaticMesh", "Material", "Texture2D", "SoundWave".',
        },
        pathPrefix: {
          type: 'string',
          description: 'Optional content path prefix filter, e.g. /Game/Environment.',
        },
        limit: {
          type: 'number',
          description: 'Maximum number of results to return (default 50).',
        },
        offset: {
          type: 'number',
          description: 'Number of results to skip for pagination (default 0).',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_ASSET_DETAILS,
    description:
      'Returns detailed, type-specific metadata for a single asset including path, type, and type-dependent properties (e.g. triangle count for meshes, resolution for textures, duration for audio).',
    inputSchema: {
      type: 'object',
      properties: {
        assetPath: {
          type: 'string',
          description: 'Full asset path, e.g. /Game/Meshes/SM_Chair.',
        },
      },
      required: ['assetPath'],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_ASSET_REFERENCES,
    description:
      'Returns the bidirectional reference graph for an asset — which assets reference it (referencers) and which assets it depends on (dependencies).',
    inputSchema: {
      type: 'object',
      properties: {
        assetPath: {
          type: 'string',
          description: 'Full asset path, e.g. /Game/Materials/M_Wood.',
        },
        direction: {
          type: 'string',
          enum: ['referencers', 'dependencies', 'both'],
          description:
            'Which direction of references to return (default "both").',
        },
      },
      required: ['assetPath'],
    },
  },
];

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient,
): Promise<ToolCallResult | null> {
  switch (name) {
    case UE_TOOL_TYPES.SEARCH_ASSETS:
    case UE_TOOL_TYPES.GET_ASSET_DETAILS:
    case UE_TOOL_TYPES.GET_ASSET_REFERENCES: {
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
