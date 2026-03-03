// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCEditorOperationsSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUEOCEditorOps, Log, All);

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCEditorOperationsSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleRequest(const FString& JsonRequest);

	// Operation handlers
	void HandleSetActorProperty(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleSpawnActor(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleDeleteActor(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleTransformActor(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleExecuteConsoleCommand(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleSetProjectSetting(const FString& RequestId, TSharedPtr<FJsonObject> Params);

	// Helpers
	AActor* FindActorByName(UWorld* World, const FString& ActorName) const;
	UClass* FindActorClass(const FString& ClassName) const;
	bool ShowConfirmationDialog(const FString& Title, const FString& Description,
		const TArray<TPair<FString, FString>>& Details);
	void SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson);
	void SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message);

	FDelegateHandle RequestDelegateHandle;
};
