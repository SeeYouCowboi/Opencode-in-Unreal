import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';
import * as fs from 'node:fs';
import * as path from 'node:path';

// ─── Project Root Resolution ──────────────────────────────────────────

/**
 * Resolve the UE project root directory.
 * Priority: UEOC_PROJECT_ROOT env var > walk 5 levels up from this file.
 * On Windows, import.meta.url produces a file:// URL with a leading slash
 * before the drive letter — strip it so Node fs APIs work correctly.
 */
function getProjectRoot(): string {
  if (process.env.UEOC_PROJECT_ROOT) {
    return process.env.UEOC_PROJECT_ROOT;
  }
  // __dirname equivalent for ESM: strip leading "/" on Windows paths (e.g. "/D:/…")
  const thisDir = path.dirname(
    new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'),
  );
  // src/tools/ → MCPServer → Resources → UnrealOpenCode → Plugins → ProjectRoot
  return path.resolve(thisDir, '..', '..', '..', '..', '..');
}

// ─── Filesystem Helpers ───────────────────────────────────────────────

function safeReadDir(dir: string): string[] {
  try {
    return fs.readdirSync(dir);
  } catch {
    return [];
  }
}

function safeReadFile(filePath: string): string | null {
  try {
    return fs.readFileSync(filePath, 'utf-8');
  } catch {
    return null;
  }
}

function safeParseJson(text: string): unknown {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function findFilesWithExtension(dir: string, ext: string, recursive: boolean): string[] {
  const results: string[] = [];
  const entries = safeReadDir(dir);
  for (const entry of entries) {
    const fullPath = path.join(dir, entry);
    try {
      const stat = fs.statSync(fullPath);
      if (stat.isFile() && entry.endsWith(ext)) {
        results.push(fullPath);
      } else if (recursive && stat.isDirectory()) {
        results.push(...findFilesWithExtension(fullPath, ext, true));
      }
    } catch {
      // Skip inaccessible entries
    }
  }
  return results;
}

function getSubdirectories(dir: string): string[] {
  const entries = safeReadDir(dir);
  const dirs: string[] = [];
  for (const entry of entries) {
    try {
      const stat = fs.statSync(path.join(dir, entry));
      if (stat.isDirectory()) {
        dirs.push(entry);
      }
    } catch {
      // Skip inaccessible entries
    }
  }
  return dirs;
}

// ─── Tool Definitions ─────────────────────────────────────────────────

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GET_PROJECT_STRUCTURE,
    description:
      'Get UE project structure including modules, plugins, and content directories',
    inputSchema: {
      type: 'object' as const,
      properties: {},
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_MODULE_DEPENDENCIES,
    description: 'Get C++ module dependencies from Build.cs files',
    inputSchema: {
      type: 'object' as const,
      properties: {
        moduleName: {
          type: 'string',
          description: 'Filter by module name (optional)',
        },
      },
      required: [],
    },
  },
  {
    name: UE_TOOL_TYPES.GET_PLUGIN_LIST,
    description: 'List all plugins in the project Plugins/ directory',
    inputSchema: {
      type: 'object' as const,
      properties: {},
      required: [],
    },
  },
];

// ─── Tool Implementations ─────────────────────────────────────────────

type ToolResult = { content: Array<{ type: 'text'; text: string }> };

function textResult(data: unknown): ToolResult {
  return { content: [{ type: 'text', text: JSON.stringify(data, null, 2) }] };
}

/**
 * get_project_structure — scans the UE project root for .uproject, Source/,
 * Content/, Config/, and Plugins/ to build a structural overview.
 */
function handleGetProjectStructure(): ToolResult {
  const root = getProjectRoot();

  // Find .uproject file
  const rootEntries = safeReadDir(root);
  const uprojectFile = rootEntries.find((f) => f.endsWith('.uproject'));
  let projectName = '';
  let modules: unknown[] = [];
  let plugins: unknown[] = [];

  if (uprojectFile) {
    projectName = path.basename(uprojectFile, '.uproject');
    const content = safeReadFile(path.join(root, uprojectFile));
    if (content) {
      const parsed = safeParseJson(content) as Record<string, unknown> | null;
      if (parsed) {
        modules = Array.isArray(parsed.Modules) ? parsed.Modules : [];
        plugins = Array.isArray(parsed.Plugins) ? parsed.Plugins : [];
      }
    }
  }

  // Source modules — find Build.cs files
  const sourceDir = path.join(root, 'Source');
  const buildCsFiles = findFilesWithExtension(sourceDir, '.Build.cs', true);
  const sourceModules = buildCsFiles.map((f) => {
    const basename = path.basename(f, '.Build.cs');
    return { name: basename, buildCsPath: path.relative(root, f) };
  });

  // Content — top-level subdirectories only
  const contentDir = path.join(root, 'Content');
  const contentDirectories = getSubdirectories(contentDir);

  // Config — .ini files
  const configDir = path.join(root, 'Config');
  const configEntries = safeReadDir(configDir);
  const configFiles = configEntries.filter((f) => f.endsWith('.ini'));

  // Plugins — .uplugin files (1 level deep)
  const pluginsDir = path.join(root, 'Plugins');
  const pluginDirs = getSubdirectories(pluginsDir);
  const upluginFiles: string[] = [];
  for (const d of pluginDirs) {
    const pluginRoot = path.join(pluginsDir, d);
    const entries = safeReadDir(pluginRoot);
    for (const e of entries) {
      if (e.endsWith('.uplugin')) {
        upluginFiles.push(path.relative(root, path.join(pluginRoot, e)));
      }
    }
  }

  return textResult({
    projectName,
    projectRoot: root,
    uprojectPath: uprojectFile ? path.join(root, uprojectFile) : null,
    modules,
    plugins,
    sourceModules,
    contentDirectories,
    configFiles,
    upluginFiles,
  });
}

/**
 * get_module_dependencies — parses Build.cs files for PublicDependencyModuleNames
 * and PrivateDependencyModuleNames.
 */
function handleGetModuleDependencies(
  args: Record<string, unknown>,
): ToolResult {
  const root = getProjectRoot();
  const sourceDir = path.join(root, 'Source');
  const filterModule = typeof args.moduleName === 'string' ? args.moduleName : null;

  const buildCsFiles = findFilesWithExtension(sourceDir, '.Build.cs', true);

  const publicDepsRegex =
    /PublicDependencyModuleNames\s*\.AddRange\s*\(\s*new\s+string\s*\[\]\s*\{([^}]*)\}/g;
  const privateDepsRegex =
    /PrivateDependencyModuleNames\s*\.AddRange\s*\(\s*new\s+string\s*\[\]\s*\{([^}]*)\}/g;

  function extractModuleNames(content: string, regex: RegExp): string[] {
    const names: string[] = [];
    let match: RegExpExecArray | null;
    while ((match = regex.exec(content)) !== null) {
      const inner = match[1];
      const stringRegex = /"([^"]+)"/g;
      let strMatch: RegExpExecArray | null;
      while ((strMatch = stringRegex.exec(inner)) !== null) {
        names.push(strMatch[1]);
      }
    }
    return names;
  }

  const results: Array<{
    moduleName: string;
    buildCsPath: string;
    publicDeps: string[];
    privateDeps: string[];
  }> = [];

  for (const filePath of buildCsFiles) {
    const moduleName = path.basename(filePath, '.Build.cs');
    if (filterModule && moduleName !== filterModule) continue;

    const content = safeReadFile(filePath);
    if (!content) continue;

    // Reset regex lastIndex since we reuse them
    publicDepsRegex.lastIndex = 0;
    privateDepsRegex.lastIndex = 0;

    const publicDeps = extractModuleNames(content, publicDepsRegex);
    const privateDeps = extractModuleNames(content, privateDepsRegex);

    results.push({
      moduleName,
      buildCsPath: path.relative(root, filePath),
      publicDeps,
      privateDeps,
    });
  }

  return textResult({ modules: results });
}

/**
 * get_plugin_list — scans Plugins/ for .uplugin files (1 level deep),
 * parses each for metadata.
 */
function handleGetPluginList(): ToolResult {
  const root = getProjectRoot();
  const pluginsDir = path.join(root, 'Plugins');
  const pluginDirs = getSubdirectories(pluginsDir);

  const plugins: Array<{
    name: string;
    version: number | null;
    versionName: string | null;
    category: string | null;
    description: string | null;
    path: string;
    modules: unknown[];
  }> = [];

  for (const d of pluginDirs) {
    const pluginRoot = path.join(pluginsDir, d);
    const entries = safeReadDir(pluginRoot);
    const upluginFile = entries.find((e) => e.endsWith('.uplugin'));
    if (!upluginFile) continue;

    const fullPath = path.join(pluginRoot, upluginFile);
    const content = safeReadFile(fullPath);
    if (!content) continue;

    const parsed = safeParseJson(content) as Record<string, unknown> | null;
    if (!parsed) continue;

    plugins.push({
      name: (parsed.FriendlyName as string) ?? (parsed.Name as string) ?? d,
      version: typeof parsed.Version === 'number' ? parsed.Version : null,
      versionName:
        typeof parsed.VersionName === 'string' ? parsed.VersionName : null,
      category:
        typeof parsed.Category === 'string' ? parsed.Category : null,
      description:
        typeof parsed.Description === 'string' ? parsed.Description : null,
      path: path.relative(root, fullPath),
      modules: Array.isArray(parsed.Modules) ? parsed.Modules : [],
    });
  }

  return textResult({ plugins });
}

// ─── Tool Call Router ─────────────────────────────────────────────────

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  _tcpClient: UETCPClient,
): Promise<ToolResult | null> {
  switch (name) {
    case UE_TOOL_TYPES.GET_PROJECT_STRUCTURE:
      return handleGetProjectStructure();
    case UE_TOOL_TYPES.GET_MODULE_DEPENDENCIES:
      return handleGetModuleDependencies(args);
    case UE_TOOL_TYPES.GET_PLUGIN_LIST:
      return handleGetPluginList();
    default:
      return null;
  }
}
