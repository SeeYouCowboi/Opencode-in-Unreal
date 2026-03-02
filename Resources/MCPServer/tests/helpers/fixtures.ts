export const sampleProjectStructure = {
  projectName: 'MyGame',
  modules: [
    { name: 'MyGame', type: 'Runtime', path: 'Source/MyGame' },
    { name: 'MyGameEditor', type: 'Editor', path: 'Source/MyGameEditor' },
  ],
  plugins: [
    { name: 'UnrealOpenCode', version: '0.1.0', enabled: true },
  ],
  contentDirectories: ['Content/Blueprints', 'Content/Materials', 'Content/Meshes'],
  configFiles: ['DefaultEngine.ini', 'DefaultGame.ini', 'DefaultEditor.ini'],
};

export const sampleCppHierarchy = {
  classes: [
    { name: 'AMyGameCharacter', parent: 'ACharacter', module: 'MyGame', isAbstract: false },
    { name: 'AMyGamePlayerController', parent: 'APlayerController', module: 'MyGame', isAbstract: false },
    { name: 'UMyGameSubsystem', parent: 'UGameInstanceSubsystem', module: 'MyGame', isAbstract: false },
  ],
};

export const sampleBlueprintList = {
  blueprints: [
    { name: 'BP_PlayerCharacter', path: '/Game/Blueprints/BP_PlayerCharacter', parentClass: 'AMyGameCharacter', type: 'Actor' },
    { name: 'WBP_HUD', path: '/Game/UI/WBP_HUD', parentClass: 'UserWidget', type: 'Widget' },
  ],
};

export const samplePingResponse = { message: 'pong', version: '0.1.0' };
