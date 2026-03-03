// ─── UE Tool Type Constants ───
// These match the C++ side exactly (FUEOCToolTypes)

export const UE_TOOL_TYPES = {
  GET_PROJECT_STRUCTURE: 'get_project_structure',
  GET_MODULE_DEPENDENCIES: 'get_module_dependencies',
  GET_PLUGIN_LIST: 'get_plugin_list',
  GET_CPP_HIERARCHY: 'get_cpp_hierarchy',
  GET_CLASS_DETAILS: 'get_class_details',
  SEARCH_CLASSES: 'search_classes',
  GET_BLUEPRINT_LIST: 'get_blueprint_list',
  GET_BLUEPRINT_DETAILS: 'get_blueprint_details',
  SEARCH_BLUEPRINTS: 'search_blueprints',
  GET_SCENE_HIERARCHY: 'get_scene_hierarchy',
  GET_ACTOR_DETAILS: 'get_actor_details',
  GET_SELECTED_ACTORS: 'get_selected_actors',
  SEARCH_ASSETS: 'search_assets',
  GET_ASSET_DETAILS: 'get_asset_details',
  GET_ASSET_REFERENCES: 'get_asset_references',
  GET_BUILD_LOGS: 'get_build_logs',
  GET_OUTPUT_LOG: 'get_output_log',
  GET_COMPILATION_STATUS: 'get_compilation_status',
  GENERATE_CODE: 'generate_code',
  SET_ACTOR_PROPERTY: 'set_actor_property',
  SPAWN_ACTOR: 'spawn_actor',
  DELETE_ACTOR: 'delete_actor',
  TRANSFORM_ACTOR: 'transform_actor',
  EXECUTE_CONSOLE_COMMAND: 'execute_console_command',
  SET_PROJECT_SETTING: 'set_project_setting',
  PING: 'ping',
} as const;

export type UEToolType = (typeof UE_TOOL_TYPES)[keyof typeof UE_TOOL_TYPES];

// ─── Default Configuration ───

export const DEFAULTS = {
  TCP_HOST: 'localhost',
  TCP_PORT: 3000,
  RECONNECT_ATTEMPTS: 3,
  RECONNECT_INTERVAL_MS: 2000,
  REQUEST_TIMEOUT_MS: 10000,
  CONNECTION_TIMEOUT_MS: 5000,
} as const;
