// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealOpenCodeEditor.h"
#include "SUnrealOpenCodePanel.h"
#include "Framework/Commands/Commands.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "FUnrealOpenCodeEditorModule"

static const FName UnrealOpenCodeTabName("UnrealOpenCodePanel");

class FUnrealOpenCodeCommands : public TCommands<FUnrealOpenCodeCommands>
{
public:
	FUnrealOpenCodeCommands()
		: TCommands<FUnrealOpenCodeCommands>(
			TEXT("UnrealOpenCode"),
			NSLOCTEXT("Contexts", "UnrealOpenCode", "UnrealOpenCode Plugin"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(OpenChatPanel, "UnrealOpenCode", "Open UnrealOpenCode AI Chat Panel", EUserInterfaceActionType::Button, FInputChord());
	}

public:
	TSharedPtr<FUICommandInfo> OpenChatPanel;
};

void FUnrealOpenCodeEditorModule::StartupModule()
{
	FUnrealOpenCodeCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FUnrealOpenCodeCommands::Get().OpenChatPanel,
		FExecuteAction::CreateRaw(this, &FUnrealOpenCodeEditorModule::OpenChatPanel),
		FCanExecuteAction());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(UnrealOpenCodeTabName, FOnSpawnTab::CreateRaw(this, &FUnrealOpenCodeEditorModule::OnSpawnTab))
		.SetDisplayName(LOCTEXT("UnrealOpenCodeTabTitle", "UnrealOpenCode"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealOpenCodeEditorModule::RegisterMenus));

	// Register console commands
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ai.status"),
		TEXT("Get UnrealOpenCode MCP server status"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UE_LOG(LogTemp, Log, TEXT("UnrealOpenCode: MCP Server status: Disconnected (server not yet started)"));
		}),
		ECVF_Default);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ai.open"),
		TEXT("Open the UnrealOpenCode AI chat panel"),
		FConsoleCommandDelegate::CreateRaw(this, &FUnrealOpenCodeEditorModule::OpenChatPanel),
		ECVF_Default);

	UE_LOG(LogTemp, Log, TEXT("UnrealOpenCode Editor module loaded"));
}

void FUnrealOpenCodeEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealOpenCodeTabName);

	FUnrealOpenCodeCommands::Unregister();

	UE_LOG(LogTemp, Log, TEXT("UnrealOpenCode Editor module unloaded"));
}

TSharedRef<SDockTab> FUnrealOpenCodeEditorModule::OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUnrealOpenCodePanel)
		];
}
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PlaceholderText", "UnrealOpenCode AI Panel — Loading..."))
		];
}

void FUnrealOpenCodeEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Window");
	
	FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
		FUnrealOpenCodeCommands::Get().OpenChatPanel,
		LOCTEXT("OpenChatButton", "AI"),
		LOCTEXT("OpenChatTooltip", "Open UnrealOpenCode AI Chat Panel"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
	);
	Entry.StyleNameOverride = "CalloutToolbar";
	Section.AddEntry(Entry);
}

void FUnrealOpenCodeEditorModule::OpenChatPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealOpenCodeTabName);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealOpenCodeEditorModule, UnrealOpenCodeEditor)
