import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GET_CPP_HIERARCHY,
    description:
      'Get UE C++ class hierarchy. Returns UClass tree with parent relationships, module info, and UCLASS specifiers.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        baseClass: {
          type: 'string',
          description:
            'Filter by base class name (e.g. "AActor"). If omitted, returns all project classes.',
        },
        includeEngine: {
          type: 'boolean',
          description: 'Include engine classes (default: false, project classes only)',
        },
        moduleFilter: { type: 'string', description: 'Filter by module name' },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_CLASS_DETAILS,
    description:
      'Get detailed info for a specific UE C++ class: all UPROPERTY fields, UFUNCTION methods, metadata.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        className: { type: 'string', description: 'Class name (e.g. "AMyCharacter" or "MyCharacter")' },
      },
      required: ['className'],
    },
  },
  {
    name: UE_TOOL_TYPES.SEARCH_CLASSES,
    description: 'Search for UE C++ classes by partial name match. Returns top 20 results.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        query: { type: 'string', description: 'Partial class name to search for' },
        includeEngine: { type: 'boolean', description: 'Include engine classes (default: false)' },
      },
      required: ['query'],
    },
  },
];

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient
): Promise<{ content: Array<{ type: 'text'; text: string }> } | null> {
  switch (name) {
    case UE_TOOL_TYPES.GET_CPP_HIERARCHY:
    case UE_TOOL_TYPES.GET_CLASS_DETAILS:
    case UE_TOOL_TYPES.SEARCH_CLASSES: {
      try {
        const response = await tcpClient.sendRequest(name, args);
        if (!response.success) {
          return {
            content: [
              {
                type: 'text' as const,
                text: `Error: ${response.error?.message ?? 'Unknown error from UE'}`,
              },
            ],
          };
        }

        return { content: [{ type: 'text' as const, text: JSON.stringify(response.data, null, 2) }] };
      } catch (err) {
        return {
          content: [
            {
              type: 'text' as const,
              text:
                `UE not connected or request failed: ${err}. ` +
                'Make sure the UE Editor is running with the UnrealOpenCode plugin enabled.',
            },
          ],
        };
      }
    }
    default:
      return null;
  }
}
