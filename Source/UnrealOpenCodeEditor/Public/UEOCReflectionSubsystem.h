#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCReflectionSubsystem.generated.h"

class FJsonObject;

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCReflectionSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleRequest(const FString& JsonRequest);
	void HandleGetCppHierarchy(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleGetClassDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void HandleSearchClasses(const FString& RequestId, TSharedPtr<FJsonObject> Params);
	void SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson);
	void SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message);

	FDelegateHandle RequestDelegateHandle;
};
