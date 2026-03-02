// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEOCTCPServer.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogUEOCTCPServer);

FUEOCTCPServer::FUEOCTCPServer()
{
}

FUEOCTCPServer::~FUEOCTCPServer()
{
	RequestStop();
}

bool FUEOCTCPServer::Start(int32 StartPort, int32 MaxPort)
{
	if (bIsRunning.IsSet())
	{
		UE_LOG(LogUEOCTCPServer, Warning, TEXT("TCP server already running on port %d"), BoundPort);
		return true;
	}

	if (MaxPort < StartPort)
	{
		MaxPort = StartPort + 10;
	}

	bool bBound = false;
	for (int32 Port = StartPort; Port <= MaxPort; ++Port)
	{
		if (TryBindPort(Port))
		{
			bBound = true;
			break;
		}
	}

	if (!bBound)
	{
		UE_LOG(LogUEOCTCPServer, Error,
			TEXT("Failed to bind TCP server on any port in range [%d, %d]"), StartPort, MaxPort);
		return false;
	}

	bStopRequested.AtomicSet(false);
	Thread = FRunnableThread::Create(this, TEXT("UEOCTCPServer"), 0, TPri_Normal);
	if (!Thread)
	{
		UE_LOG(LogUEOCTCPServer, Error, TEXT("Failed to create TCP server thread"));
		DestroyListenerSocket();
		return false;
	}

	UE_LOG(LogUEOCTCPServer, Log, TEXT("TCP server started on port %d"), BoundPort);
	return true;
}

void FUEOCTCPServer::RequestStop()
{
	bStopRequested.AtomicSet(true);

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	DisconnectClient();
	DestroyListenerSocket();
}

void FUEOCTCPServer::SendJsonResponse(const FString& JsonResponse)
{
	OutgoingJsonQueue.Enqueue(JsonResponse);
}

void FUEOCTCPServer::Tick()
{
	FString JsonRequest;
	while (IncomingJsonQueue.Dequeue(JsonRequest))
	{
		if (OnRawRequestReceived.IsBound())
		{
			OnRawRequestReceived.Execute(JsonRequest);
		}
		else
		{
			UE_LOG(LogUEOCTCPServer, Warning,
				TEXT("Received request but no handler bound. Dropping message."));
		}
	}
}

bool FUEOCTCPServer::Init()
{
	return true;
}

uint32 FUEOCTCPServer::Run()
{
	bIsRunning.AtomicSet(true);

	UE_LOG(LogUEOCTCPServer, Log, TEXT("TCP server thread running, listening on port %d"), BoundPort);

	while (!bStopRequested.IsSet())
	{
		if (!ClientSocket)
		{
			TryAcceptConnection();
		}
		else
		{
			uint32 PendingDataSize = 0;
			if (ClientSocket->HasPendingData(PendingDataSize))
			{
				FString JsonMessage;
				if (ReadMessage(JsonMessage))
				{
					if (!JsonMessage.IsEmpty())
					{
						IncomingJsonQueue.Enqueue(MoveTemp(JsonMessage));
					}
				}
				else
				{
					UE_LOG(LogUEOCTCPServer, Warning, TEXT("MCP client disconnected (read error)"));
					DisconnectClient();
					continue;
				}
			}

			FString OutgoingJson;
			while (OutgoingJsonQueue.Dequeue(OutgoingJson))
			{
				if (!WriteMessage(OutgoingJson))
				{
					UE_LOG(LogUEOCTCPServer, Warning, TEXT("MCP client disconnected (write error)"));
					DisconnectClient();
					break;
				}
			}

			if (ClientSocket)
			{
				ESocketConnectionState ConnectionState = ClientSocket->GetConnectionState();
				if (ConnectionState == SCS_ConnectionError)
				{
					UE_LOG(LogUEOCTCPServer, Warning, TEXT("MCP client connection lost"));
					DisconnectClient();
				}
			}
		}

		FPlatformProcess::Sleep(IdleSleepSeconds);
	}

	bIsRunning.AtomicSet(false);
	return 0;
}

void FUEOCTCPServer::Stop()
{
	bStopRequested.AtomicSet(true);
}

void FUEOCTCPServer::Exit()
{
	UE_LOG(LogUEOCTCPServer, Log, TEXT("TCP server thread exiting"));
	DisconnectClient();
}

bool FUEOCTCPServer::TryBindPort(int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogUEOCTCPServer, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}

	FSocket* NewSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UEOCTCPServer"), false);
	if (!NewSocket)
	{
		UE_LOG(LogUEOCTCPServer, Error, TEXT("Failed to create listener socket"));
		return false;
	}

	NewSocket->SetReuseAddr(true);
	NewSocket->SetNonBlocking(true);
	NewSocket->SetRecvErr();

	TSharedRef<FInternetAddr> BindAddr = SocketSubsystem->CreateInternetAddr();
	BindAddr->SetIp(FIPv4Address::InternalLoopback.Value);
	BindAddr->SetPort(Port);

	if (!NewSocket->Bind(*BindAddr))
	{
		UE_LOG(LogUEOCTCPServer, Log, TEXT("Port %d in use, trying next..."), Port);
		SocketSubsystem->DestroySocket(NewSocket);
		return false;
	}

	if (!NewSocket->Listen(1))
	{
		UE_LOG(LogUEOCTCPServer, Error, TEXT("Failed to listen on port %d"), Port);
		SocketSubsystem->DestroySocket(NewSocket);
		return false;
	}

	DestroyListenerSocket();

	ListenerSocket = NewSocket;
	BoundPort = Port;

	UE_LOG(LogUEOCTCPServer, Log, TEXT("Bound to port %d"), Port);
	return true;
}

bool FUEOCTCPServer::TryAcceptConnection()
{
	if (!ListenerSocket)
	{
		return false;
	}

	bool bHasPendingConnection = false;
	if (!ListenerSocket->HasPendingConnection(bHasPendingConnection) || !bHasPendingConnection)
	{
		return false;
	}

	FSocket* NewClient = ListenerSocket->Accept(TEXT("MCP Client"));
	if (!NewClient)
	{
		return false;
	}

	NewClient->SetNonBlocking(true);
	NewClient->SetRecvErr();

	DisconnectClient();

	ClientSocket = NewClient;
	bClientConnected.AtomicSet(true);
	ReceiveBuffer.Reset();

	UE_LOG(LogUEOCTCPServer, Log, TEXT("MCP client connected from %s"),
		*NewClient->GetDescription());

	return true;
}

void FUEOCTCPServer::DisconnectClient()
{
	if (ClientSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			ClientSocket->Close();
			SocketSubsystem->DestroySocket(ClientSocket);
		}
		ClientSocket = nullptr;
		bClientConnected.AtomicSet(false);
		ReceiveBuffer.Reset();

		UE_LOG(LogUEOCTCPServer, Log, TEXT("MCP client disconnected, returning to listening state"));
	}
}

void FUEOCTCPServer::DestroyListenerSocket()
{
	if (ListenerSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			ListenerSocket->Close();
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
		BoundPort = 0;
	}
}

/*
 * Message framing protocol (must match MCP server TypeScript implementation):
 *   [4 bytes: big-endian uint32 length N] [N bytes: UTF-8 JSON body]
 *
 * ReadMessage accumulates data in ReceiveBuffer across calls. Returns true
 * with empty OutJson when a partial message is buffered (not an error).
 * Returns false on protocol violations or socket errors.
 */
bool FUEOCTCPServer::ReadMessage(FString& OutJson)
{
	if (!ClientSocket)
	{
		return false;
	}

	uint8 TempBuffer[ReadChunkSize];
	int32 BytesRead = 0;

	while (ClientSocket->Recv(TempBuffer, ReadChunkSize, BytesRead, ESocketReceiveFlags::None))
	{
		if (BytesRead <= 0)
		{
			break;
		}

		ReceiveBuffer.Append(TempBuffer, BytesRead);

		if (ReceiveBuffer.Num() > MaxMessageSize + 4)
		{
			UE_LOG(LogUEOCTCPServer, Error,
				TEXT("Receive buffer exceeded max message size (%d bytes). Disconnecting client."),
				MaxMessageSize);
			return false;
		}
	}

	if (ReceiveBuffer.Num() < 4)
	{
		OutJson.Empty();
		return true;
	}

	int32 MessageLength = 0;
	if (!ParseLengthPrefix(ReceiveBuffer.GetData(), MessageLength))
	{
		return false;
	}

	if (MessageLength <= 0)
	{
		UE_LOG(LogUEOCTCPServer, Warning, TEXT("Received message with zero or negative length: %d"), MessageLength);
		return false;
	}

	if (MessageLength > MaxMessageSize)
	{
		UE_LOG(LogUEOCTCPServer, Error,
			TEXT("Message length %d exceeds max allowed size %d. Rejecting."),
			MessageLength, MaxMessageSize);
		return false;
	}

	const int32 TotalRequired = 4 + MessageLength;
	if (ReceiveBuffer.Num() < TotalRequired)
	{
		OutJson.Empty();
		return true;
	}

	FUTF8ToTCHAR Converter(
		reinterpret_cast<const ANSICHAR*>(ReceiveBuffer.GetData() + 4),
		MessageLength
	);
	OutJson = FString(Converter.Length(), Converter.Get());

	ReceiveBuffer.RemoveAt(0, TotalRequired, EAllowShrinking::No);

	if (OutJson.IsEmpty())
	{
		UE_LOG(LogUEOCTCPServer, Warning, TEXT("Received empty JSON message"));
		return false;
	}

	UE_LOG(LogUEOCTCPServer, Verbose, TEXT("Received message (%d bytes): %s"),
		MessageLength, *OutJson.Left(200));

	return true;
}

bool FUEOCTCPServer::WriteMessage(const FString& Json)
{
	if (!ClientSocket)
	{
		return false;
	}

	FTCHARToUTF8 Converter(*Json);
	const int32 JsonLength = Converter.Length();

	if (JsonLength <= 0)
	{
		UE_LOG(LogUEOCTCPServer, Warning, TEXT("Attempted to send empty message"));
		return false;
	}

	if (JsonLength > MaxMessageSize)
	{
		UE_LOG(LogUEOCTCPServer, Error,
			TEXT("Outgoing message too large (%d bytes, max %d)"), JsonLength, MaxMessageSize);
		return false;
	}

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(4 + JsonLength);

	// 4-byte big-endian uint32 length prefix per protocol spec
	Buffer[0] = static_cast<uint8>((JsonLength >> 24) & 0xFF);
	Buffer[1] = static_cast<uint8>((JsonLength >> 16) & 0xFF);
	Buffer[2] = static_cast<uint8>((JsonLength >> 8) & 0xFF);
	Buffer[3] = static_cast<uint8>(JsonLength & 0xFF);

	FMemory::Memcpy(Buffer.GetData() + 4, Converter.Get(), JsonLength);

	int32 TotalSent = 0;
	const int32 TotalToSend = Buffer.Num();

	while (TotalSent < TotalToSend)
	{
		int32 BytesSent = 0;
		bool bSendOk = ClientSocket->Send(
			Buffer.GetData() + TotalSent,
			TotalToSend - TotalSent,
			BytesSent
		);

		if (!bSendOk || BytesSent <= 0)
		{
			UE_LOG(LogUEOCTCPServer, Warning,
				TEXT("Send failed at %d/%d bytes"), TotalSent, TotalToSend);
			return false;
		}

		TotalSent += BytesSent;
	}

	UE_LOG(LogUEOCTCPServer, Verbose, TEXT("Sent message (%d bytes): %s"),
		JsonLength, *Json.Left(200));

	return true;
}

bool FUEOCTCPServer::ParseLengthPrefix(const uint8* Data, int32& OutLength) const
{
	// Big-endian uint32: [MSB][byte2][byte1][LSB]
	const uint32 RawLength =
		(static_cast<uint32>(Data[0]) << 24) |
		(static_cast<uint32>(Data[1]) << 16) |
		(static_cast<uint32>(Data[2]) << 8) |
		(static_cast<uint32>(Data[3]));

	OutLength = static_cast<int32>(RawLength);
	return true;
}
