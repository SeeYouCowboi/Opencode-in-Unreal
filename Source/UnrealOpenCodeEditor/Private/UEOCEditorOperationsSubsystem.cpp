// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEOCEditorOperationsSubsystem.h"
#include "UEOCTCPServerSubsystem.h"
#include "UnrealOpenCodeProtocol.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY(LogUEOCEditorOps);

namespace
{
	FString EscapeJsonString(const FString& InValue)
	{
		FString Escaped = InValue;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Escaped;
	}

}

// ────────────────────────────────────────────────────────────────────
// Lifecycle
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UUEOCTCPServerSubsystem>();

	if (GEditor == nullptr)
	{
		return;
	}

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		RequestDelegateHandle = TCPSubsystem->OnJsonRequestReceived.AddUObject(
			this, &UUEOCEditorOperationsSubsystem::HandleRequest);

		UE_LOG(LogUEOCEditorOps, Log,
			TEXT("Editor operations subsystem initialized and bound to TCP server"));
	}
	else
	{
		UE_LOG(LogUEOCEditorOps, Warning,
			TEXT("TCP server subsystem not available — editor operation handlers will not work"));
	}
}

void UUEOCEditorOperationsSubsystem::Deinitialize()
{
	if (GEditor != nullptr && RequestDelegateHandle.IsValid())
	{
		UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
		if (TCPSubsystem)
		{
			TCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
		}
	}
	RequestDelegateHandle.Reset();

	UE_LOG(LogUEOCEditorOps, Log, TEXT("Editor operations subsystem deinitialized"));

	Super::Deinitialize();
}

// ────────────────────────────────────────────────────────────────────
// Request dispatch
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	FString Type;
	FString RequestId;
	if (!Root->TryGetStringField(TEXT("type"), Type) || !Root->TryGetStringField(TEXT("id"), RequestId))
	{
		return;
	}

	TSharedPtr<FJsonObject> Params = Root->GetObjectField(TEXT("params"));

	if (Type == UEOCToolTypes::SetActorProperty)
	{
		HandleSetActorProperty(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::ExecuteConsoleCommand)
	{
		HandleExecuteConsoleCommand(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::SetProjectSetting)
	{
		HandleSetProjectSetting(RequestId, Params);
	}
}

// ────────────────────────────────────────────────────────────────────
// set_actor_property
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleSetActorProperty(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::SetActorProperty;

	FString ActorName, PropertyName, NewValue, ComponentName;
	if (!Params.IsValid()
		|| !Params->TryGetStringField(TEXT("actorName"), ActorName)
		|| !Params->TryGetStringField(TEXT("propertyName"), PropertyName)
		|| !Params->TryGetStringField(TEXT("value"), NewValue))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required params: actorName, propertyName, value"));
		return;
	}
	Params->TryGetStringField(TEXT("componentName"), ComponentName);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("No editor world available"));
		return;
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		SendErrorResponse(RequestId, Type, -1, FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}

	// Determine target object (actor or component)
	UObject* TargetObject = Actor;
	if (!ComponentName.IsEmpty())
	{
		UActorComponent* Comp = nullptr;
		for (UActorComponent* C : Actor->GetComponents())
		{
			if (C && C->GetName() == ComponentName)
			{
				Comp = C;
				break;
			}
		}
		if (!Comp)
		{
			SendErrorResponse(RequestId, Type, -1,
				FString::Printf(TEXT("Component not found: %s on actor %s"), *ComponentName, *ActorName));
			return;
		}
		TargetObject = Comp;
	}

	// Find property via reflection
	FProperty* Prop = FindFProperty<FProperty>(TargetObject->GetClass(), *PropertyName);
	if (!Prop)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Property not found: %s on %s"), *PropertyName, *TargetObject->GetClass()->GetName()));
		return;
	}

	// Get current value as string
	FString OldValue;
	void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
	Prop->ExportTextItem(OldValue, ValueAddr, nullptr, TargetObject, PPF_None, nullptr);

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Actor"), Actor->GetActorLabel()));
	if (!ComponentName.IsEmpty())
	{
		Details.Add(TPair<FString, FString>(TEXT("Component"), ComponentName));
	}
	Details.Add(TPair<FString, FString>(TEXT("Property"), PropertyName));
	Details.Add(TPair<FString, FString>(TEXT("Current Value"), OldValue));
	Details.Add(TPair<FString, FString>(TEXT("New Value"), NewValue));

	if (!ShowConfirmationDialog(TEXT("Set Actor Property"), TEXT("Modify a property value on an actor or component."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	// Execute with undo support
	FScopedTransaction Transaction(NSLOCTEXT("UnrealOpenCode", "SetActorProperty", "Set Actor Property"));
	TargetObject->Modify();

	TargetObject->PreEditChange(Prop);
	const TCHAR* ImportResult = Prop->ImportText(*NewValue, ValueAddr, PPF_None, TargetObject);
	if (ImportResult == nullptr)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Failed to set property value. Ensure format matches property type. Property: %s, Value: %s"),
				*PropertyName, *NewValue));
		return;
	}

	FPropertyChangedEvent ChangedEvent(Prop);
	TargetObject->PostEditChangeProperty(ChangedEvent);

	if (UPackage* Package = TargetObject->GetOutermost())
	{
		Package->MarkPackageDirty();
	}

	GEditor->RedrawLevelEditingViewports();

	// Build response
	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("actorName"), Actor->GetName());
	Data->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("propertyName"), PropertyName);
	Data->SetStringField(TEXT("oldValue"), OldValue);
	Data->SetStringField(TEXT("newValue"), NewValue);
	if (!ComponentName.IsEmpty())
	{
		Data->SetStringField(TEXT("componentName"), ComponentName);
	}
	SendResponse(RequestId, Type, Data);
}

// ────────────────────────────────────────────────────────────────────
// execute_console_command
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleExecuteConsoleCommand(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::ExecuteConsoleCommand;

	FString Command;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("command"), Command))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required param: command"));
		return;
	}

	if (Command.IsEmpty())
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Command must not be empty"));
		return;
	}

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Command"), Command));

	if (!ShowConfirmationDialog(TEXT("Execute Console Command"),
		TEXT("Execute a console command in the Unreal Editor. This may have significant effects."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	// Capture output
	FStringOutputDevice OutputDevice;
	GEngine->Exec(World, *Command, OutputDevice);

	FString Output = OutputDevice;

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("command"), Command);
	Data->SetStringField(TEXT("output"), Output);
	Data->SetBoolField(TEXT("executed"), true);
	SendResponse(RequestId, Type, Data);
}

// ────────────────────────────────────────────────────────────────────
// set_project_setting
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleSetProjectSetting(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::SetProjectSetting;

	FString Section, Key, Value;
	if (!Params.IsValid()
		|| !Params->TryGetStringField(TEXT("section"), Section)
		|| !Params->TryGetStringField(TEXT("key"), Key)
		|| !Params->TryGetStringField(TEXT("value"), Value))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required params: section, key, value"));
		return;
	}

	FString ConfigFile;
	if (!Params->TryGetStringField(TEXT("configFile"), ConfigFile))
	{
		ConfigFile = TEXT("Engine");
	}

	// Map config name to actual file path
	FString ConfigFilePath;
	if (ConfigFile.Equals(TEXT("Engine"), ESearchCase::IgnoreCase))
	{
		ConfigFilePath = GEngineIni;
	}
	else if (ConfigFile.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
	{
		ConfigFilePath = GGameIni;
	}
	else if (ConfigFile.Equals(TEXT("Input"), ESearchCase::IgnoreCase))
	{
		ConfigFilePath = GInputIni;
	}
	else if (ConfigFile.Equals(TEXT("Editor"), ESearchCase::IgnoreCase))
	{
		ConfigFilePath = GEditorIni;
	}
	else if (ConfigFile.Equals(TEXT("EditorPerProjectUserSettings"), ESearchCase::IgnoreCase))
	{
		ConfigFilePath = GEditorPerProjectIni;
	}
	else
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Unknown config file: %s (supported: Engine, Game, Input, Editor, EditorPerProjectUserSettings)"), *ConfigFile));
		return;
	}

	// Get current value for display
	FString OldValue;
	GConfig->GetString(*Section, *Key, OldValue, ConfigFilePath);

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Config File"), ConfigFile));
	Details.Add(TPair<FString, FString>(TEXT("Section"), Section));
	Details.Add(TPair<FString, FString>(TEXT("Key"), Key));
	Details.Add(TPair<FString, FString>(TEXT("Current Value"), OldValue.IsEmpty() ? TEXT("(not set)") : OldValue));
	Details.Add(TPair<FString, FString>(TEXT("New Value"), Value));

	if (!ShowConfirmationDialog(TEXT("Set Project Setting"),
		TEXT("Modify a project configuration setting (.ini file)."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	GConfig->SetString(*Section, *Key, *Value, ConfigFilePath);
	GConfig->Flush(false, ConfigFilePath);

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("configFile"), ConfigFile);
	Data->SetStringField(TEXT("section"), Section);
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("oldValue"), OldValue);
	Data->SetStringField(TEXT("newValue"), Value);
	SendResponse(RequestId, Type, Data);
}

// ────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────

AActor* UUEOCEditorOperationsSubsystem::FindActorByName(UWorld* World, const FString& ActorName) const
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
		{
			return Actor;
		}
	}
	return nullptr;
}

bool UUEOCEditorOperationsSubsystem::ShowConfirmationDialog(
	const FString& Title,
	const FString& Description,
	const TArray<TPair<FString, FString>>& Details)
{
	bool bConfirmed = false;

	// Build details text
	FString DetailsText;
	for (const auto& Pair : Details)
	{
		DetailsText += FString::Printf(TEXT("%s:  %s\n"), *Pair.Key, *Pair.Value);
	}

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(Title))
		.ClientSize(FVector2D(620.0f, 400.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true);

	Dialog->SetContent(
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Title))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			// Description
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Description))
				.AutoWrapText(true)
			]

			// Details
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(FText::FromString(DetailsText))
						.Font(FAppStyle::Get().GetFontStyle(TEXT("Mono")))
						.AutoWrapText(true)
					]
				]
			]

			// Buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Lambda([Dialog]()
					{
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Confirm")))
					.OnClicked_Lambda([Dialog, &bConfirmed]()
					{
						bConfirmed = true;
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, nullptr, false);
	return bConfirmed;
}

// ────────────────────────────────────────────────────────────────────
// Response helpers
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("data"), DataJson);
	Response->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		TCPSubsystem->SendJsonResponse(OutputString);
	}
}

void UUEOCEditorOperationsSubsystem::SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShareable(new FJsonObject);
	ErrorObj->SetNumberField(TEXT("code"), Code);
	ErrorObj->SetStringField(TEXT("message"), Message);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetObjectField(TEXT("error"), ErrorObj);
	Response->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		TCPSubsystem->SendJsonResponse(OutputString);
	}

	UE_LOG(LogUEOCEditorOps, Warning, TEXT("Error [%s] %d: %s"), *Type, Code, *Message);
}
