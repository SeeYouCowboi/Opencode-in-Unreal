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
#include "GameFramework/Actor.h"
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

	FVector ParseVector(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, const FVector& Default)
	{
		const TSharedPtr<FJsonObject>* VecObj = nullptr;
		if (Obj.IsValid() && Obj->TryGetObjectField(FieldName, VecObj) && VecObj && (*VecObj).IsValid())
		{
			double X = Default.X, Y = Default.Y, Z = Default.Z;
			(*VecObj)->TryGetNumberField(TEXT("x"), X);
			(*VecObj)->TryGetNumberField(TEXT("y"), Y);
			(*VecObj)->TryGetNumberField(TEXT("z"), Z);
			return FVector(X, Y, Z);
		}
		return Default;
	}

	FRotator ParseRotator(const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, const FRotator& Default)
	{
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		if (Obj.IsValid() && Obj->TryGetObjectField(FieldName, RotObj) && RotObj && (*RotObj).IsValid())
		{
			double Pitch = Default.Pitch, Yaw = Default.Yaw, Roll = Default.Roll;
			(*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
			return FRotator(Pitch, Yaw, Roll);
		}
		return Default;
	}

	TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	TSharedPtr<FJsonObject> RotatorToJson(const FRotator& R)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetNumberField(TEXT("pitch"), R.Pitch);
		Obj->SetNumberField(TEXT("yaw"), R.Yaw);
		Obj->SetNumberField(TEXT("roll"), R.Roll);
		return Obj;
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
	else if (Type == UEOCToolTypes::SpawnActor)
	{
		HandleSpawnActor(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::DeleteActor)
	{
		HandleDeleteActor(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::TransformActor)
	{
		HandleTransformActor(RequestId, Params);
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
// spawn_actor
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleSpawnActor(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::SpawnActor;

	FString ClassName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("className"), ClassName))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required param: className"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("No editor world available"));
		return;
	}

	UClass* ActorClass = FindActorClass(ClassName);
	if (!ActorClass)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Actor class not found: %s"), *ClassName));
		return;
	}

	FVector Location = ParseVector(Params, TEXT("location"), FVector::ZeroVector);
	FRotator Rotation = ParseRotator(Params, TEXT("rotation"), FRotator::ZeroRotator);

	FString Label;
	Params->TryGetStringField(TEXT("label"), Label);

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Class"), ActorClass->GetName()));
	Details.Add(TPair<FString, FString>(TEXT("Location"), Location.ToString()));
	Details.Add(TPair<FString, FString>(TEXT("Rotation"), Rotation.ToString()));
	if (!Label.IsEmpty())
	{
		Details.Add(TPair<FString, FString>(TEXT("Label"), Label));
	}

	if (!ShowConfirmationDialog(TEXT("Spawn Actor"), TEXT("Spawn a new actor in the current level."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	// Spawn with undo support
	FScopedTransaction Transaction(NSLOCTEXT("UnrealOpenCode", "SpawnActor", "Spawn Actor"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnTransform(Rotation, Location);
	AActor* NewActor = World->SpawnActor(ActorClass, &SpawnTransform, SpawnParams);
	if (!NewActor)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Failed to spawn actor of class: %s"), *ClassName));
		return;
	}

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	// Select the new actor in the editor
	GEditor->SelectNone(true, true, false);
	GEditor->SelectActor(NewActor, true, true);
	GEditor->RedrawLevelEditingViewports();

	// Build response
	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("name"), NewActor->GetName());
	Data->SetStringField(TEXT("label"), NewActor->GetActorLabel());
	Data->SetStringField(TEXT("class"), ActorClass->GetName());
	Data->SetObjectField(TEXT("location"), VectorToJson(NewActor->GetActorLocation()));
	Data->SetObjectField(TEXT("rotation"), RotatorToJson(NewActor->GetActorRotation()));
	SendResponse(RequestId, Type, Data);
}

// ────────────────────────────────────────────────────────────────────
// delete_actor
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleDeleteActor(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::DeleteActor;

	FString ActorName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("actorName"), ActorName))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required param: actorName"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("No editor world available"));
		return;
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}

	const FString DeletedName = Actor->GetName();
	const FString DeletedLabel = Actor->GetActorLabel();
	const FString DeletedClass = Actor->GetClass()->GetName();

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Actor"), DeletedLabel));
	Details.Add(TPair<FString, FString>(TEXT("Class"), DeletedClass));
	Details.Add(TPair<FString, FString>(TEXT("Location"), Actor->GetActorLocation().ToString()));

	if (!ShowConfirmationDialog(TEXT("Delete Actor"), TEXT("Permanently delete an actor from the current level."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	// Delete with undo support
	FScopedTransaction Transaction(NSLOCTEXT("UnrealOpenCode", "DeleteActor", "Delete Actor"));
	bool bDestroyed = World->EditorDestroyActor(Actor, false);

	if (!bDestroyed)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Failed to delete actor: %s"), *ActorName));
		return;
	}

	GEditor->RedrawLevelEditingViewports();

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("deletedName"), DeletedName);
	Data->SetStringField(TEXT("deletedLabel"), DeletedLabel);
	Data->SetStringField(TEXT("deletedClass"), DeletedClass);
	SendResponse(RequestId, Type, Data);
}

// ────────────────────────────────────────────────────────────────────
// transform_actor
// ────────────────────────────────────────────────────────────────────

void UUEOCEditorOperationsSubsystem::HandleTransformActor(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	const FString Type = UEOCToolTypes::TransformActor;

	FString ActorName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("actorName"), ActorName))
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("Missing required param: actorName"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("No editor world available"));
		return;
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		SendErrorResponse(RequestId, Type, -1,
			FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}

	// Parse optional transform components — use current values as defaults
	const FVector CurrentLocation = Actor->GetActorLocation();
	const FRotator CurrentRotation = Actor->GetActorRotation();
	const FVector CurrentScale = Actor->GetActorScale3D();

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;

	bool bHasLocation = Params->TryGetObjectField(TEXT("location"), LocationObj);
	bool bHasRotation = Params->TryGetObjectField(TEXT("rotation"), RotationObj);
	bool bHasScale = Params->TryGetObjectField(TEXT("scale"), ScaleObj);

	if (!bHasLocation && !bHasRotation && !bHasScale)
	{
		SendErrorResponse(RequestId, Type, -1, TEXT("At least one of location, rotation, or scale must be specified"));
		return;
	}

	FVector NewLocation = bHasLocation ? ParseVector(Params, TEXT("location"), CurrentLocation) : CurrentLocation;
	FRotator NewRotation = bHasRotation ? ParseRotator(Params, TEXT("rotation"), CurrentRotation) : CurrentRotation;
	FVector NewScale = bHasScale ? ParseVector(Params, TEXT("scale"), CurrentScale) : CurrentScale;

	// Confirmation dialog
	TArray<TPair<FString, FString>> Details;
	Details.Add(TPair<FString, FString>(TEXT("Actor"), Actor->GetActorLabel()));
	if (bHasLocation)
	{
		Details.Add(TPair<FString, FString>(TEXT("Location"), FString::Printf(TEXT("%s -> %s"), *CurrentLocation.ToString(), *NewLocation.ToString())));
	}
	if (bHasRotation)
	{
		Details.Add(TPair<FString, FString>(TEXT("Rotation"), FString::Printf(TEXT("%s -> %s"), *CurrentRotation.ToString(), *NewRotation.ToString())));
	}
	if (bHasScale)
	{
		Details.Add(TPair<FString, FString>(TEXT("Scale"), FString::Printf(TEXT("%s -> %s"), *CurrentScale.ToString(), *NewScale.ToString())));
	}

	if (!ShowConfirmationDialog(TEXT("Transform Actor"), TEXT("Modify the transform of an actor."), Details))
	{
		SendErrorResponse(RequestId, Type, -2, TEXT("Operation cancelled by user"));
		return;
	}

	// Apply with undo support
	FScopedTransaction Transaction(NSLOCTEXT("UnrealOpenCode", "TransformActor", "Transform Actor"));
	Actor->Modify();

	if (bHasLocation)
	{
		Actor->SetActorLocation(NewLocation);
	}
	if (bHasRotation)
	{
		Actor->SetActorRotation(NewRotation);
	}
	if (bHasScale)
	{
		Actor->SetActorScale3D(NewScale);
	}

	if (UPackage* Package = Actor->GetOutermost())
	{
		Package->MarkPackageDirty();
	}

	GEditor->RedrawLevelEditingViewports();

	// Build response
	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("actorName"), Actor->GetName());
	Data->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Data->SetObjectField(TEXT("location"), VectorToJson(Actor->GetActorLocation()));
	Data->SetObjectField(TEXT("rotation"), RotatorToJson(Actor->GetActorRotation()));
	Data->SetObjectField(TEXT("scale"), VectorToJson(Actor->GetActorScale3D()));
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

UClass* UUEOCEditorOperationsSubsystem::FindActorClass(const FString& ClassName) const
{
	// Try exact name
	UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName);

	// Try with 'A' prefix for actor classes
	if (!Class && !ClassName.StartsWith(TEXT("A")))
	{
		Class = FindObject<UClass>(ANY_PACKAGE, *(TEXT("A") + ClassName));
	}

	// Try loading as asset path (for Blueprint classes)
	if (!Class)
	{
		Class = LoadClass<AActor>(nullptr, *ClassName);
	}

	// Verify it's an Actor subclass
	if (Class && !Class->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogUEOCEditorOps, Warning,
			TEXT("Class '%s' found but is not an Actor subclass"), *ClassName);
		return nullptr;
	}

	return Class;
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
