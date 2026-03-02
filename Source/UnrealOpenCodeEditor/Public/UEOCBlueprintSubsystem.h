#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCBlueprintSubsystem.generated.h"

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCBlueprintSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleRequest(const FString& JsonRequest);
	void HandleGetBlueprintList(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetBlueprintDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleSearchBlueprints(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson);
	void SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message);

	FDelegateHandle RequestDelegateHandle;
};
