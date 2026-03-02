#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEOCTCPServerSubsystem.h"
#include "UEOCCodeGenSubsystem.generated.h"

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCCodeGenSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleRequest(const FString& JsonRequest);

	void HandleGenerateCode(const FString& RequestId,
		const FString& FilePath,
		const FString& Content,
		const FString& Description);

	bool ShowConfirmationDialog(const FString& FilePath,
		const FString& Content,
		const FString& Description,
		bool bFileExists);

	bool WriteFile(const FString& AbsolutePath, const FString& Content, bool bCreateBackup);

	void SendResponse(const FString& RequestId, bool bSuccess,
		const FString& DataJson, const FString& ErrorMessage = TEXT(""));

	FDelegateHandle RequestDelegateHandle;
	TWeakObjectPtr<UUEOCTCPServerSubsystem> TCPSubsystem;
};
