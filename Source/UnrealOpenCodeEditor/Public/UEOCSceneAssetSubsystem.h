// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCSceneAssetSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUEOCSceneAssetSubsystem, Log, All);

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCSceneAssetSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleRequest(const FString& JsonRequest);
	void HandleGetSceneHierarchy(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetActorDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetSelectedActors(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleSearchAssets(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetAssetDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetAssetReferences(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson);
	void SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message);

	FDelegateHandle RequestDelegateHandle;
};
