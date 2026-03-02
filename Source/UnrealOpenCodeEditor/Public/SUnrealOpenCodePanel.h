// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

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
    TArray<FUEOCChatMessageSegment> Segments;  // Parsed from Text

    /** Parse Text into Segments, splitting on ``` markers */
    void ParseMessageSegments();
};

/** A chat session containing multiple messages */
struct FUEOCChatSession
{
    FString SessionId;    // FGuid::NewGuid().ToString()
    FString Title;        // Auto-set from first user message
    TArray<TSharedPtr<FUEOCChatMessage>> Messages;
    FDateTime CreatedAt;
    bool bIsActive = false;
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
    void AddMessage(const FString& Sender, const FString& Text, bool bIsUser);

    /** Set the TCP connection status displayed in the context bar */
    void SetConnectionStatus(bool bConnected, const FString& StatusText = TEXT(""));

private:
    //~ Session Management
    /** Create a new chat session */
    void NewSession();
    
    /** Load a session by index into the active view */
    void LoadSession(int32 Index);
    
    /** Toggle visibility of the session sidebar */
    void ToggleSessionSidebar();

    /** Save a session to disk */
    void SaveSession(int32 SessionIndex);

    /** Save the sessions index file */
    void SaveSessionIndex();

    /** Load all sessions from disk */
    void LoadAllSessions();

    /** Delete a session and remove from disk */
    void DeleteSession(int32 Index);
    
    /** Generate a row widget for the session list */
    TSharedRef<ITableRow> OnGenerateSessionRow(
        TSharedPtr<FUEOCChatSession> Session,
        const TSharedRef<STableViewBase>& OwnerTable);
    
    /** Handle session selection */
    void OnSessionSelected(TSharedPtr<FUEOCChatSession> Session, ESelectInfo::Type SelectInfo);

    //~ Message Handling
    /** Handle user pressing Enter or clicking Send */
    FReply OnSendMessage();
    
    /** Handle text committed in the input box */
    void OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitType);
    
    /** Generate a row widget for the message list */
    TSharedRef<ITableRow> OnGenerateMessageRow(
        TSharedPtr<FUEOCChatMessage> Message,
        const TSharedRef<STableViewBase>& OwnerTable);
    
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
    /** Get visibility for session sidebar */
    EVisibility GetSessionSidebarVisibility() const;
    
    /** Get the text for the connection status indicator */
    FText GetConnectionStatusText() const;
    
    /** Get the color for the connection status indicator */
    FSlateColor GetConnectionStatusColor() const;
    
    /** Get the text for the context bar */
    FText GetContextBarText() const;

    //~ State: Sessions
    TArray<TSharedPtr<FUEOCChatSession>> Sessions;
    int32 CurrentSessionIndex = -1;
    TSharedPtr<SListView<TSharedPtr<FUEOCChatSession>>> SessionListView;
    bool bSessionSidebarVisible = true;
    
    //~ State: Current Session Messages
    TArray<TSharedPtr<FUEOCChatMessage>> Messages;
    TSharedPtr<SListView<TSharedPtr<FUEOCChatMessage>>> MessageListView;
    TSharedPtr<SScrollBox> MessageScrollBox;
    
    //~ State: Input
    TSharedPtr<SEditableTextBox> InputTextBox;
    FText CurrentInputText;
    
    //~ State: Connection
    bool bIsConnected = false;
    FString ConnectionStatusString = TEXT("Disconnected");
    
    //~ Widget References
    TSharedPtr<STextBlock> ConnectionStatusTextBlock;
    TSharedPtr<STextBlock> ContextBarTextBlock;
};
