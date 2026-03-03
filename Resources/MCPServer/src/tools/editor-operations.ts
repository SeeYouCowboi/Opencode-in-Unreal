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
    name: UE_TOOL_TYPES.SPAWN_ACTOR,
    description:
      'Spawn a new actor in the current editor level. Supports C++ class names (e.g., "StaticMeshActor", "PointLight") and Blueprint class paths.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        className: {
          type: 'string',
          description:
            'Actor class name (e.g., "StaticMeshActor", "PointLight", "CameraActor") or full Blueprint class path',
        },
        location: {
          type: 'object',
          description: 'Spawn location',
          properties: {
            x: { type: 'number', description: 'X coordinate' },
            y: { type: 'number', description: 'Y coordinate' },
            z: { type: 'number', description: 'Z coordinate' },
          },
        },
        rotation: {
          type: 'object',
          description: 'Spawn rotation',
          properties: {
            pitch: { type: 'number', description: 'Pitch in degrees' },
            yaw: { type: 'number', description: 'Yaw in degrees' },
            roll: { type: 'number', description: 'Roll in degrees' },
          },
        },
        label: {
          type: 'string',
          description: 'Optional display label for the actor in the editor',
        },
      },
      required: ['className'],
    },
  },
  {
    name: UE_TOOL_TYPES.DELETE_ACTOR,
    description:
      'Delete an actor from the current editor level. Supports undo.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        actorName: {
          type: 'string',
          description: 'Name or label of the actor to delete',
        },
      },
      required: ['actorName'],
    },
  },
  {
    name: UE_TOOL_TYPES.TRANSFORM_ACTOR,
    description:
      'Set the location, rotation, and/or scale of an actor. Only specified components are changed; others remain unchanged.',
    inputSchema: {
      type: 'object' as const,
      properties: {
        actorName: {
          type: 'string',
          description: 'Name or label of the actor to transform',
        },
        location: {
          type: 'object',
          description: 'New world location (omit to keep current)',
          properties: {
            x: { type: 'number' },
            y: { type: 'number' },
            z: { type: 'number' },
          },
        },
        rotation: {
          type: 'object',
          description: 'New world rotation in degrees (omit to keep current)',
          properties: {
            pitch: { type: 'number' },
            yaw: { type: 'number' },
            roll: { type: 'number' },
          },
        },
        scale: {
          type: 'object',
          description: 'New 3D scale (omit to keep current)',
          properties: {
            x: { type: 'number' },
            y: { type: 'number' },
            z: { type: 'number' },
          },
        },
      },
      required: ['actorName'],
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
  UE_TOOL_TYPES.SPAWN_ACTOR,
  UE_TOOL_TYPES.DELETE_ACTOR,
  UE_TOOL_TYPES.TRANSFORM_ACTOR,
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
