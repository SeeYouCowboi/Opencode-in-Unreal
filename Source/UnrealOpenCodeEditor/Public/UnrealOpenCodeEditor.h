// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class UNREALOPENCODEEDITOR_API FUnrealOpenCodeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<class FUICommandList> PluginCommands;

	void RegisterMenus();
	void OpenChatPanel();
	TSharedRef<class SDockTab> OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs);
};
