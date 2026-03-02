// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "UEOCTCPServerSubsystem.generated.h"

class FUEOCTCPServer;

DECLARE_LOG_CATEGORY_EXTERN(LogUEOCTCPServerSubsystem, Log, All);

UCLASS()
class UNREALOPENCODEEDITOR_API UUEOCTCPServerSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="UnrealOpenCode")
	bool IsConnected() const;

	UFUNCTION(BlueprintCallable, Category="UnrealOpenCode")
	int32 GetBoundPort() const;

	UFUNCTION(BlueprintCallable, Category="UnrealOpenCode")
	bool IsServerRunning() const;

	void SendJsonResponse(const FString& JsonResponse);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnJsonRequestReceived, const FString& /* JsonRequest */);
	FOnJsonRequestReceived OnJsonRequestReceived;

private:
	bool HandleTick(float DeltaTime);
	void OnRawRequestReceived(const FString& JsonRequest);

	TSharedPtr<FUEOCTCPServer> TCPServer;
	FTSTicker::FDelegateHandle TickHandle;

	int32 ConfiguredStartPort = 3000;
	int32 ConfiguredMaxPort = 3010;
};
