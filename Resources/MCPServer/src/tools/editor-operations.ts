import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

// Operations with confirmation dialogs need a longer timeout (2 minutes)
const CONFIRM_TIMEOUT_MS = 120_000;

// ─── Tool Definitions ─────────────────────────────────────────────────

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.SET_ACTOR_PROPERTY,
    description:
      'Set a property value on an actor or its component using UE reflection. Supports any UPROPERTY type. The value must be in UE text import format (e.g., "(X=1.0,Y=2.0,Z=3.0)" for FVector).',
    inputSchema: {
      type: 'object' as const,
      properties: {
        actorName: {
          type: 'string',
          description: 'Name or label of the actor to modify',
        },
        propertyName: {
          type: 'string',
          description:
            'Name of the UPROPERTY to set (e.g., "bHidden", "Mobility")',
        },
        value: {
          type: 'string',
          description:
            'New value as a string in UE text import format',
        },
        componentName: {
          type: 'string',
          description:
            'Optional: name of a specific component on the actor to modify instead of the actor itself',
        },
      },
      required: ['actorName', 'propertyName', 'value'],
    },
  },
  {
    name: UE_TOOL_TYPES.EXECUTE_CONSOLE_COMMAND,
    description:
      'Execute an Unreal Engine console command in the editor. Use with caution — commands can have significant effects. Output is captured and returned.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        command: {
          type: 'string',
          description:
            'Console command to execute (e.g., "stat fps", "t.MaxFPS 60")',
        },
      },
      required: ['command'],
    },
  },
  {
    name: UE_TOOL_TYPES.SET_PROJECT_SETTING,
    description:
      'Modify a project configuration setting in a .ini config file. Changes are saved immediately.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        section: {
          type: 'string',
          description: 'INI section name (e.g., "/Script/Engine.RendererSettings")',
        },
        key: {
          type: 'string',
          description: 'Setting key name',
        },
        value: {
          type: 'string',
          description: 'New value for the setting',
        },
        configFile: {
          type: 'string',
          enum: ['Engine', 'Game', 'Input', 'Editor', 'EditorPerProjectUserSettings'],
          description: 'Which config file to modify (default: Engine)',
        },
      },
      required: ['section', 'key', 'value'],
    },
  },
];

// ─── Tool Call Router ─────────────────────────────────────────────────

type ToolResult = { content: Array<{ type: 'text'; text: string }> };

const EDITOR_OP_TOOLS = new Set<string>([
  UE_TOOL_TYPES.SET_ACTOR_PROPERTY,
  UE_TOOL_TYPES.EXECUTE_CONSOLE_COMMAND,
  UE_TOOL_TYPES.SET_PROJECT_SETTING,
]);

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient,
): Promise<ToolResult | null> {
  if (!EDITOR_OP_TOOLS.has(name)) {
    return null;
  }

  try {
    const response = await tcpClient.sendRequest(name, args, CONFIRM_TIMEOUT_MS);
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
