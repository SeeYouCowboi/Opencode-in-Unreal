import { z } from 'zod';

// ─── Request from MCP Server → UE Plugin ───

export interface UERequest {
  id: string;              // UUID
  type: string;            // e.g., "get_project_structure", "get_cpp_hierarchy"
  params: Record<string, unknown>;
  timestamp: number;
}

export const UERequestSchema = z.object({
  id: z.string().uuid(),
  type: z.string(),
  params: z.record(z.unknown()),
  timestamp: z.number(),
});

// ─── Response from UE Plugin → MCP Server ───

export interface UEResponse {
  id: string;              // Matches request UUID
  type: string;
  success: boolean;
  data?: unknown;
  error?: { code: number; message: string };
  timestamp: number;
}

export const UEResponseSchema = z.object({
  id: z.string(),
  type: z.string(),
  success: z.boolean(),
  data: z.unknown().optional(),
  error: z.object({
    code: z.number(),
    message: z.string(),
  }).optional(),
  timestamp: z.number(),
});
