// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"


/** A single segment of a chat message (plain text or code block) */
struct FUEOCChatMessageSegment
{
    FString Text;
    bool bIsCode = false;
};

/** A single chat message displayed in the panel */
struct FUEOCChatMessage
{
    FString Sender;   // "User" or "Assistant"
    FString Text;     // Full raw text (kept for compatibility)
    FDateTime Timestamp;
    bool bIsUser;
    FString AgentName;  // Which agent responded (empty for user/system messages)
    TArray<FUEOCChatMessageSegment> Segments;  // Parsed from Text

    /** Parse Text into Segments, splitting on ``` markers */
    void ParseMessageSegments();
};

/** A chat session containing multiple messages */
struct FUEOCChatSession
{
    FString SessionId;         // Local GUID
    FString OpenCodeSessionId; // Opencode server session ID (e.g. ses_xxx)
    FString Title;             // Display title fetched from opencode
    TArray<TSharedPtr<FUEOCChatMessage>> Messages;
    FDateTime CreatedAt;
    bool bIsActive = false;
};

/** A live tool call entry tracked during streaming */
struct FLiveToolEntry
{
    FString DisplayName;  // Formatted name shown in UI
    bool bCompleted = false;
};

/** Inline span types for markdown rendering */
enum class EUEOCSpanType : uint8 { Plain, Code, Bold, Italic };

/** A single inline-formatted span within a markdown line */
struct FUEOCInlineSpan
{
    FString Text;
    EUEOCSpanType Type = EUEOCSpanType::Plain;
};

/** Line types for markdown rendering */
enum class EUEOCLineType : uint8 { Normal, Header1, Header2, Header3, BulletList, NumberedList, HRule, Empty };

/** A single parsed markdown line */
struct FUEOCMarkdownLine
{
    EUEOCLineType Type  = EUEOCLineType::Normal;
    FString ListPrefix;       // "1." or "•" for list items
    int32   IndentLevel = 0;  // Indent levels (2 spaces per level)
    TArray<FUEOCInlineSpan> Spans;
};

/**
 * SUnrealOpenCodePanel — Core Slate UI for the UnrealOpenCode AI chat panel.
 * Provides: scrollable message history + text input + send button + session management + code blocks.
 * Full AI integration is added in Task 20.
 */
class UNREALOPENCODEEDITOR_API SUnrealOpenCodePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SUnrealOpenCodePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    
    /** Add a message to the current chat session */
    void AddMessage(const FString& Sender, const FString& Text, bool bIsUser, const FString& InAgentName = TEXT(""));

    /** Set the TCP connection status displayed in the context bar */
    void SetConnectionStatus(bool bConnected, const FString& StatusText = TEXT(""));

private:
    //~ Session Management
    /** Create a new chat session */
    void NewSession();
    
    /** Load a session by index into the active view */
    void LoadSession(int32 Index);

    /** Save a session to disk */
    void SaveSession(int32 SessionIndex);

    /** Save the sessions index file */
    void SaveSessionIndex();

    /** Load all sessions from disk */
    void LoadAllSessions();

    /** Delete a session and remove from disk */
    void DeleteSession(int32 Index);

    //~ Message Handling
    /** Rebuild the message scroll box content (call after adding messages or loading session) */
    void RebuildMessageArea();
    /** Rebuild the session sidebar list */
    void RebuildSessionList();
    /** Build a single session item widget for the sidebar */
    TSharedRef<SWidget> BuildSessionItem(int32 SessionIndex);
    
    /** Build a single message widget for the scroll box */
    TSharedRef<SWidget> BuildMessageWidget(TSharedPtr<FUEOCChatMessage> Message);
    
    /** Get the content switcher index (0 = welcome, 1 = chat) */
    int32 GetContentSwitcherIndex() const { return Messages.Num() == 0 ? 0 : 1; }
    
    /** Handle user pressing Enter or clicking Send */
    FReply OnSendMessage();
    
    /** Handle text committed in the input box */
    void OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitType);
    
    /** Build the content for a message (handles segments) */
    TSharedRef<SWidget> BuildMessageContent(TSharedPtr<FUEOCChatMessage> Message);
    
    /** Build a code block widget with Copy and Insert buttons */
    TSharedRef<SWidget> BuildCodeBlockWidget(const FString& CodeText);
    
    /** Scroll to the bottom of the message list */
    void ScrollToBottom();
    
    /** Copy message text to clipboard */
    FReply OnCopyMessage(const FString& MessageText);

    //~ Code Block Actions
    /** Copy code text to clipboard */
    FReply OnCopyCode(const FString& CodeText);
    
    /** Insert code into a file via save dialog */
    FReply OnInsertCode(const FString& CodeText);

    //~ UI Helpers
    /** Get the text for the connection status dot */
    FText GetConnectionStatusText() const;
    
    /** Get the color for the connection status dot */
    FSlateColor GetConnectionStatusColor() const;
    
    /** Get the current session title for display */
    FText GetCurrentSessionTitle() const;

    //~ State: Sessions
    TArray<TSharedPtr<FUEOCChatSession>> Sessions;
    int32 CurrentSessionIndex = -1;
    
    //~ State: Current Session Messages
    TArray<TSharedPtr<FUEOCChatMessage>> Messages;
    
    //~ State: Input
    TSharedPtr<SEditableTextBox> InputTextBox;
    FText CurrentInputText;
    
    //~ State: Connection
    bool bIsConnected = false;
    FString ConnectionStatusString = TEXT("Disconnected");

    //~ State: opencode HTTP client
    int32 OpenCodePort = 4096;
    FString OpenCodeSessionId;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PendingHttpRequest;
    int32 ThinkingMessageIndex = -1;

    //~ State: Live polling for real-time streaming display
    TMap<FString, FLiveToolEntry> LiveToolParts;   // partId → {displayName, bCompleted}
    TSet<FString> ShownTextPartIds;               // text part IDs already incorporated
    TSet<FString> KnownPartIds;                   // part IDs present before user message (pre-flight snapshot)
    FString LiveText;                             // accumulated assistant text so far
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PollingHttpRequest;
    FTSTicker::FDelegateHandle PollingTickHandle;
    bool bPollingActive = false;
    bool bPollingRequestInFlight = false;

    void CreateOpenCodeSession(TFunction<void()> OnComplete);
    void FetchAvailableAgents();
    FText GetSelectedAgentText() const;
    void SendMessageToOpenCode(const FString& UserText);
    /** Fetch the opencode-generated title for a session and update the sidebar */
    void FetchAndUpdateSessionTitle(int32 SessionIndex);
    static FString ExtractAiResponseText(const FString& ResponseJson);

    //~ Live polling
    void StartLivePolling();
    void StopLivePolling();
    bool OnLivePollingTick(float DeltaTime);
    void FirePollingRequest();
    /** Mark all part IDs in Json as already-known (pre-flight snapshot) */
    void SnapshotExistingPartIds(const FString& Json);
    /** Process polling GET response, extract new parts */
    void ProcessPollingResponse(const FString& Json);
    /** Rebuild the thinking placeholder with live tool + text data */
    void UpdateLiveThinkingMessage();
    /** Rebuild only the message scroll area (no sidebar rebuild) */
    void RefreshMessageAreaOnly();
    /** Format a raw tool name for display */
    static FString FormatToolDisplayName(const FString& RawToolName);
    //~ Markdown rendering (static — no instance state needed)
    static TArray<FUEOCInlineSpan>   ParseInlineSpans(const FString& Line);
    static TArray<FUEOCMarkdownLine> ParseMarkdownLines(const FString& Text);
    static TSharedRef<SWidget>       BuildMarkdownWidget(const TArray<FUEOCMarkdownLine>& Lines);
    static TSharedRef<SWidget>       BuildInlineSpansWidget(const TArray<FUEOCInlineSpan>& Spans, const FLinearColor& BaseColor, bool bBold);

    //~ Widget References
    TSharedPtr<SWidgetSwitcher> ContentSwitcher;
    TSharedPtr<SScrollBox> MessageScrollBox;
    TSharedPtr<SScrollBox> SessionListScrollBox;
    TSharedPtr<STextBlock> ConnectionStatusTextBlock;

    //~ State: Agent selector
    TArray<TSharedPtr<FString>> AvailableAgentOptions;
    TSharedPtr<FString> SelectedAgent;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> AgentComboBox;
};
