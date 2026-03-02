// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Containers/Queue.h"

// Forward declarations for protocol types (defined in UnrealOpenCodeProtocol.h from Task 3)
struct FUEOCRequest;
struct FUEOCResponse;

DECLARE_LOG_CATEGORY_EXTERN(LogUEOCTCPServer, Log, All);

/**
 * Delegate fired on game thread when a JSON request is received from the MCP client.
 * The FString parameter is the raw JSON string of the request.
 */
DECLARE_DELEGATE_OneParam(FOnUEOCRawRequestReceived, const FString& /* JsonRequest */);

/**
 * FUEOCTCPServer — Background TCP server using FRunnable.
 *
 * Listens on a configurable port (default 3000) for a single MCP client connection.
 * Implements length-prefixed JSON message framing:
 *   - 4-byte big-endian uint32 length prefix
 *   - UTF-8 encoded JSON body
 *
 * Thread-safe message queues bridge the background I/O thread and the game thread:
 *   - IncomingJsonQueue: background → game thread (requests from MCP)
 *   - OutgoingJsonQueue: game thread → background (responses to MCP)
 *
 * Usage:
 *   1. Create instance
 *   2. Bind OnRawRequestReceived delegate
 *   3. Call Start(Port) — creates listener socket + background thread
 *   4. Call Tick() from game thread each frame to dispatch queued requests
 *   5. Call SendJsonResponse() from any thread to queue outgoing messages
 *   6. Call Stop() to shut down (blocks until thread exits)
 */
class UNREALOPENCODECORE_API FUEOCTCPServer : public FRunnable
{
public:
	FUEOCTCPServer();
	virtual ~FUEOCTCPServer();

	/**
	 * Start listening on the given port range.
	 * Tries ports from StartPort to MaxPort, returns true if any succeeded.
	 */
	bool Start(int32 StartPort, int32 MaxPort = -1);

	/** Stop the server and wait for the background thread to exit. */
	void RequestStop();

	/** Is the background thread running? */
	bool IsRunning() const { return (bool)bIsRunning; }

	/** Is a MCP client currently connected? */
	bool IsClientConnected() const { return (bool)bClientConnected; }

	/** Get the port the server is bound to (0 if not listening). */
	int32 GetBoundPort() const { return BoundPort; }

	/**
	 * Queue a JSON response to send to the MCP client.
	 * Thread-safe — can be called from any thread.
	 */
	void SendJsonResponse(const FString& JsonResponse);

	/**
	 * Delegate called on the game thread when a raw JSON request is received.
	 * Bind this to dispatch requests to context providers.
	 */
	FOnUEOCRawRequestReceived OnRawRequestReceived;

	/**
	 * Tick — must be called from the game thread each frame.
	 * Drains IncomingJsonQueue and fires OnRawRequestReceived for each message.
	 */
	void Tick();

	// ---- FRunnable interface ----
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	/** Try to bind a listener socket on the given port. */
	bool TryBindPort(int32 Port);

	/** Non-blocking accept of a pending client connection. Returns true if a new client was accepted. */
	bool TryAcceptConnection();

	/** Read a complete length-prefixed message from the client socket. Returns false on error/disconnect. */
	bool ReadMessage(FString& OutJson);

	/** Write a length-prefixed message to the client socket. Returns false on error/disconnect. */
	bool WriteMessage(const FString& Json);

	/** Parse the 4-byte big-endian length prefix from the receive buffer. */
	bool ParseLengthPrefix(const uint8* Data, int32& OutLength) const;

	/** Disconnect the current client and reset state. */
	void DisconnectClient();

	/** Destroy the listener socket. */
	void DestroyListenerSocket();

	// ---- Sockets ----
	FSocket* ListenerSocket = nullptr;
	FSocket* ClientSocket = nullptr;

	// ---- Threading ----
	FRunnableThread* Thread = nullptr;

	/** Set when the background thread is actively running. */
	FThreadSafeBool bIsRunning;

	/** Set to request the background thread to stop. */
	FThreadSafeBool bStopRequested;

	/** Set when a client is connected. */
	FThreadSafeBool bClientConnected;

	// ---- Thread-safe queues ----
	/** Background thread → Game thread (incoming JSON requests) */
	TQueue<FString, EQueueMode::Mpsc> IncomingJsonQueue;

	/** Game thread → Background thread (outgoing JSON responses) */
	TQueue<FString, EQueueMode::Mpsc> OutgoingJsonQueue;

	// ---- Configuration ----
	int32 BoundPort = 0;

	/** Partial receive buffer for incomplete messages. */
	TArray<uint8> ReceiveBuffer;

	/** Maximum allowed message size (10 MB). */
	static constexpr int32 MaxMessageSize = 10 * 1024 * 1024;

	/** Size of the temporary read buffer per Recv call. */
	static constexpr int32 ReadChunkSize = 65536;

	/** Sleep interval in the Run() loop when idle (milliseconds). */
	static constexpr float IdleSleepSeconds = 0.005f; // 5ms
};
