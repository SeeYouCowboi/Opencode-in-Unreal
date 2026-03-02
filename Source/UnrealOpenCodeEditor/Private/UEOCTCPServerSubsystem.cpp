// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEOCTCPServerSubsystem.h"
#include "UEOCTCPServer.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY(LogUEOCTCPServerSubsystem);

void UUEOCTCPServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GConfig->GetInt(TEXT("UnrealOpenCode"), TEXT("TcpPort"), ConfiguredStartPort, GEngineIni);
	GConfig->GetInt(TEXT("UnrealOpenCode"), TEXT("TcpMaxPort"), ConfiguredMaxPort, GEngineIni);

	if (ConfiguredMaxPort < ConfiguredStartPort)
	{
		ConfiguredMaxPort = ConfiguredStartPort + 10;
	}

	UE_LOG(LogUEOCTCPServerSubsystem, Log,
		TEXT("Initializing TCP server subsystem (port range: %d-%d)"),
		ConfiguredStartPort, ConfiguredMaxPort);

	TCPServer = MakeShared<FUEOCTCPServer>();

	TCPServer->OnRawRequestReceived.BindUObject(this, &UUEOCTCPServerSubsystem::OnRawRequestReceived);

	if (!TCPServer->Start(ConfiguredStartPort, ConfiguredMaxPort))
	{
		UE_LOG(LogUEOCTCPServerSubsystem, Error,
			TEXT("Failed to start TCP server. MCP client will not be able to connect."));
		return;
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UUEOCTCPServerSubsystem::HandleTick),
		0.0f
	);

	UE_LOG(LogUEOCTCPServerSubsystem, Log,
		TEXT("TCP server subsystem initialized. Listening on port %d"), TCPServer->GetBoundPort());
}

void UUEOCTCPServerSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);

	if (TCPServer.IsValid())
	{
		UE_LOG(LogUEOCTCPServerSubsystem, Log, TEXT("Stopping TCP server..."));
		TCPServer->RequestStop();
		TCPServer.Reset();
	}

	UE_LOG(LogUEOCTCPServerSubsystem, Log, TEXT("TCP server subsystem deinitialized"));

	Super::Deinitialize();
}

bool UUEOCTCPServerSubsystem::IsConnected() const
{
	return TCPServer.IsValid() && TCPServer->IsClientConnected();
}

int32 UUEOCTCPServerSubsystem::GetBoundPort() const
{
	return TCPServer.IsValid() ? TCPServer->GetBoundPort() : 0;
}

bool UUEOCTCPServerSubsystem::IsServerRunning() const
{
	return TCPServer.IsValid() && TCPServer->IsRunning();
}

void UUEOCTCPServerSubsystem::SendJsonResponse(const FString& JsonResponse)
{
	if (TCPServer.IsValid())
	{
		TCPServer->SendJsonResponse(JsonResponse);
	}
	else
	{
		UE_LOG(LogUEOCTCPServerSubsystem, Warning,
			TEXT("Cannot send response: TCP server not initialized"));
	}
}

bool UUEOCTCPServerSubsystem::HandleTick(float DeltaTime)
{
	if (TCPServer.IsValid())
	{
		TCPServer->Tick();
	}
	return true;
}

void UUEOCTCPServerSubsystem::OnRawRequestReceived(const FString& JsonRequest)
{
	UE_LOG(LogUEOCTCPServerSubsystem, Verbose,
		TEXT("Dispatching request to %d handler(s)"), OnJsonRequestReceived.IsBound() ? 1 : 0);

	OnJsonRequestReceived.Broadcast(JsonRequest);
}
