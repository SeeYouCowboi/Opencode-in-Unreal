import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { mkdirSync, writeFileSync, rmSync } from 'fs';
import { join } from 'path';
import * as os from 'os';
import { handleToolCall } from '../../src/tools/project-structure.js';
import { UE_TOOL_TYPES } from '../../src/types/constants.js';
import type { UETCPClient } from '../../src/tcp-client.js';

describe('project-structure tools', () => {
  let tmpDir: string;
  const savedProjectRoot = process.env.UEOC_PROJECT_ROOT;
  const mockClient = {} as UETCPClient; // Not used by filesystem-only tools

  beforeEach(() => {
    tmpDir = join(os.tmpdir(), 'ueoc-test-' + Date.now() + '-' + Math.random().toString(36).slice(2));
    mkdirSync(tmpDir, { recursive: true });
    process.env.UEOC_PROJECT_ROOT = tmpDir;
  });

  afterEach(() => {
    if (savedProjectRoot === undefined) delete process.env.UEOC_PROJECT_ROOT;
    else process.env.UEOC_PROJECT_ROOT = savedProjectRoot;
    rmSync(tmpDir, { recursive: true, force: true });
  });

  describe('get_project_structure', () => {
    it('returns full project structure with .uproject, Source, Content, Config, Plugins', async () => {
      // Create minimal project structure
      writeFileSync(join(tmpDir, 'MyGame.uproject'), JSON.stringify({
        Modules: [{ Name: 'MyGame', Type: 'Runtime', LoadingPhase: 'Default' }],
        Plugins: [{ Name: 'UnrealOpenCode', Enabled: true }],
      }));
      mkdirSync(join(tmpDir, 'Source', 'MyGame'), { recursive: true });
      writeFileSync(join(tmpDir, 'Source', 'MyGame', 'MyGame.Build.cs'), 'using UnrealBuildTool;');
      mkdirSync(join(tmpDir, 'Content', 'Blueprints'), { recursive: true });
      mkdirSync(join(tmpDir, 'Content', 'Materials'), { recursive: true });
      mkdirSync(join(tmpDir, 'Config'), { recursive: true });
      writeFileSync(join(tmpDir, 'Config', 'DefaultEngine.ini'), '[Engine]');
      writeFileSync(join(tmpDir, 'Config', 'DefaultGame.ini'), '[Game]');
      mkdirSync(join(tmpDir, 'Plugins', 'MyPlugin'), { recursive: true });
      writeFileSync(join(tmpDir, 'Plugins', 'MyPlugin', 'MyPlugin.uplugin'), JSON.stringify({
        FriendlyName: 'My Plugin',
        Version: 1,
        VersionName: '1.0',
        Modules: [{ Name: 'MyPlugin', Type: 'Runtime' }],
      }));

      const result = await handleToolCall(UE_TOOL_TYPES.GET_PROJECT_STRUCTURE, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.projectName).toBe('MyGame');
      expect(data.sourceModules).toHaveLength(1);
      expect(data.sourceModules[0].name).toBe('MyGame');
      expect(data.contentDirectories).toContain('Blueprints');
      expect(data.contentDirectories).toContain('Materials');
      expect(data.configFiles).toContain('DefaultEngine.ini');
      expect(data.configFiles).toContain('DefaultGame.ini');
      expect(data.upluginFiles).toHaveLength(1);
      expect(data.modules).toHaveLength(1);
      expect(data.plugins).toHaveLength(1);
    });

    it('handles empty project directory gracefully', async () => {
      const result = await handleToolCall(UE_TOOL_TYPES.GET_PROJECT_STRUCTURE, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.projectName).toBe('');
      expect(data.sourceModules).toEqual([]);
      expect(data.contentDirectories).toEqual([]);
      expect(data.configFiles).toEqual([]);
      expect(data.upluginFiles).toEqual([]);
    });

    it('handles malformed .uproject JSON gracefully', async () => {
      writeFileSync(join(tmpDir, 'Bad.uproject'), 'not valid json {{{');

      const result = await handleToolCall(UE_TOOL_TYPES.GET_PROJECT_STRUCTURE, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.projectName).toBe('Bad');
      expect(data.modules).toEqual([]);
      expect(data.plugins).toEqual([]);
    });
  });

  describe('get_module_dependencies', () => {
    it('parses Build.cs for public and private dependencies', async () => {
      mkdirSync(join(tmpDir, 'Source', 'MyGame'), { recursive: true });
      writeFileSync(join(tmpDir, 'Source', 'MyGame', 'MyGame.Build.cs'), `
        using UnrealBuildTool;
        public class MyGame : ModuleRules {
          public MyGame(ReadOnlyTargetRules Target) : base(Target) {
            PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
            PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
          }
        }
      `);

      const result = await handleToolCall(UE_TOOL_TYPES.GET_MODULE_DEPENDENCIES, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.modules).toHaveLength(1);
      expect(data.modules[0].moduleName).toBe('MyGame');
      expect(data.modules[0].publicDeps).toEqual(['Core', 'CoreUObject', 'Engine']);
      expect(data.modules[0].privateDeps).toEqual(['Slate', 'SlateCore']);
    });

    it('filters by module name', async () => {
      mkdirSync(join(tmpDir, 'Source', 'ModA'), { recursive: true });
      mkdirSync(join(tmpDir, 'Source', 'ModB'), { recursive: true });
      writeFileSync(join(tmpDir, 'Source', 'ModA', 'ModA.Build.cs'), `
        PublicDependencyModuleNames.AddRange(new string[] { "Core" });
      `);
      writeFileSync(join(tmpDir, 'Source', 'ModB', 'ModB.Build.cs'), `
        PublicDependencyModuleNames.AddRange(new string[] { "Engine" });
      `);

      const result = await handleToolCall(UE_TOOL_TYPES.GET_MODULE_DEPENDENCIES, { moduleName: 'ModA' }, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.modules).toHaveLength(1);
      expect(data.modules[0].moduleName).toBe('ModA');
      expect(data.modules[0].publicDeps).toEqual(['Core']);
    });

    it('returns empty when no Source directory exists', async () => {
      const result = await handleToolCall(UE_TOOL_TYPES.GET_MODULE_DEPENDENCIES, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.modules).toEqual([]);
    });
  });

  describe('get_plugin_list', () => {
    it('lists plugins with full metadata', async () => {
      mkdirSync(join(tmpDir, 'Plugins', 'TestPlugin'), { recursive: true });
      writeFileSync(join(tmpDir, 'Plugins', 'TestPlugin', 'TestPlugin.uplugin'), JSON.stringify({
        FriendlyName: 'Test Plugin',
        Version: 2,
        VersionName: '2.0.0',
        Category: 'Testing',
        Description: 'A test plugin',
        Modules: [{ Name: 'TestPlugin', Type: 'Runtime' }],
      }));

      const result = await handleToolCall(UE_TOOL_TYPES.GET_PLUGIN_LIST, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.plugins).toHaveLength(1);
      expect(data.plugins[0].name).toBe('Test Plugin');
      expect(data.plugins[0].version).toBe(2);
      expect(data.plugins[0].versionName).toBe('2.0.0');
      expect(data.plugins[0].category).toBe('Testing');
      expect(data.plugins[0].description).toBe('A test plugin');
      expect(data.plugins[0].modules).toHaveLength(1);
    });

    it('handles missing Plugins directory', async () => {
      const result = await handleToolCall(UE_TOOL_TYPES.GET_PLUGIN_LIST, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.plugins).toEqual([]);
    });

    it('skips plugins with malformed .uplugin files', async () => {
      mkdirSync(join(tmpDir, 'Plugins', 'BadPlugin'), { recursive: true });
      writeFileSync(join(tmpDir, 'Plugins', 'BadPlugin', 'BadPlugin.uplugin'), 'not json');
      mkdirSync(join(tmpDir, 'Plugins', 'GoodPlugin'), { recursive: true });
      writeFileSync(join(tmpDir, 'Plugins', 'GoodPlugin', 'GoodPlugin.uplugin'), JSON.stringify({
        FriendlyName: 'Good Plugin',
        Version: 1,
      }));

      const result = await handleToolCall(UE_TOOL_TYPES.GET_PLUGIN_LIST, {}, mockClient);
      expect(result).not.toBeNull();

      const data = JSON.parse(result!.content[0].text);
      expect(data.plugins).toHaveLength(1);
      expect(data.plugins[0].name).toBe('Good Plugin');
    });
  });

  it('returns null for unknown tool name', async () => {
    const result = await handleToolCall('unknown_tool', {}, mockClient);
    expect(result).toBeNull();
  });
});
