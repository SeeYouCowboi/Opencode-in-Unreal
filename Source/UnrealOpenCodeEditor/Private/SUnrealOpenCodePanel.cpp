// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUnrealOpenCodePanel.h"
#include "Editor.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Framework/Text/TextLayout.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

namespace UEOCJsonHelper
{
    /** Extract a JSON string value for a given key. Handles escaped characters. */
    static FString ExtractStringValue(const FString& Json, const FString& Key)
    {
        FString SearchKey = FString::Printf(TEXT("\"%s\":\""), *Key);
        int32 KeyPos = Json.Find(SearchKey, ESearchCase::CaseSensitive);
        if (KeyPos == INDEX_NONE) return FString();
        
        int32 ValueStart = KeyPos + SearchKey.Len();
        int32 Pos = ValueStart;
        while (Pos < Json.Len())
        {
            if (Json[Pos] == TEXT('\\')) { Pos += 2; continue; }
            if (Json[Pos] == TEXT('"')) break;
            Pos++;
        }
        
        // Unescape character by character
        FString Raw = Json.Mid(ValueStart, Pos - ValueStart);
        FString Result;
        Result.Reserve(Raw.Len());
        for (int32 i = 0; i < Raw.Len(); ++i)
        {
            if (Raw[i] == TEXT('\\') && i + 1 < Raw.Len())
            {
                TCHAR Next = Raw[i + 1];
                if (Next == TEXT('n')) { Result += TEXT('\n'); i++; }
                else if (Next == TEXT('r')) { Result += TEXT('\r'); i++; }
                else if (Next == TEXT('t')) { Result += TEXT('\t'); i++; }
                else if (Next == TEXT('"')) { Result += TEXT('"'); i++; }
                else if (Next == TEXT('\\')) { Result += TEXT('\\'); i++; }
                else { Result += Raw[i]; }
            }
            else
            {
                Result += Raw[i];
            }
        }
        return Result;
    }
    
    /** Extract a JSON bool value for a given key */
    static bool ExtractBoolValue(const FString& Json, const FString& Key)
    {
        FString SearchKey = FString::Printf(TEXT("\"%s\":"), *Key);
        int32 KeyPos = Json.Find(SearchKey, ESearchCase::CaseSensitive);
        if (KeyPos == INDEX_NONE) return false;
        
        int32 ValueStart = KeyPos + SearchKey.Len();
        FString Remaining = Json.Mid(ValueStart).TrimStart();
        return Remaining.StartsWith(TEXT("true"));
    }
}

//~ FUEOCChatMessage Implementation

void FUEOCChatMessage::ParseMessageSegments()
{
    Segments.Empty();
    
    if (Text.IsEmpty())
    {
        return;
    }
    
    const FString CodeBlockMarker = TEXT("```");
    int32 CurrentPos = 0;
    bool bInCodeBlock = false;
    
    while (CurrentPos < Text.Len())
    {
        int32 MarkerPos = Text.Find(CodeBlockMarker, ESearchCase::CaseSensitive, ESearchDir::FromStart, CurrentPos);
        
        if (MarkerPos == INDEX_NONE)
        {
            // No more markers, add remaining text
            FString RemainingText = Text.Mid(CurrentPos);
            if (!RemainingText.IsEmpty())
            {
                FUEOCChatMessageSegment Segment;
                Segment.Text = RemainingText;
                Segment.bIsCode = bInCodeBlock;
                Segments.Add(Segment);
            }
            break;
        }
        
        // Add text before marker
        FString BeforeMarker = Text.Mid(CurrentPos, MarkerPos - CurrentPos);
        if (!BeforeMarker.IsEmpty() || bInCodeBlock)
        {
            FUEOCChatMessageSegment Segment;
            Segment.Text = BeforeMarker;
            Segment.bIsCode = bInCodeBlock;
            Segments.Add(Segment);
        }
        
        // Skip the marker and check for language identifier
        CurrentPos = MarkerPos + CodeBlockMarker.Len();
        
        // If entering a code block, skip language identifier (e.g., ```cpp)
        if (!bInCodeBlock)
        {
            int32 NewlinePos = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CurrentPos);
            if (NewlinePos != INDEX_NONE && NewlinePos < Text.Find(CodeBlockMarker, ESearchCase::CaseSensitive, ESearchDir::FromStart, CurrentPos))
            {
                CurrentPos = NewlinePos + 1; // Skip past the newline
            }
        }
        
        bInCodeBlock = !bInCodeBlock;
    }
    
    // If we ended inside a code block, close it
    if (bInCodeBlock && CurrentPos < Text.Len())
    {
        FUEOCChatMessageSegment Segment;
        Segment.Text = Text.Mid(CurrentPos);
        Segment.bIsCode = true;
        Segments.Add(Segment);
    }
}

//~ SUnrealOpenCodePanel Implementation

void SUnrealOpenCodePanel::Construct(const FArguments& InArgs)
{
    // Load existing sessions from disk
    LoadAllSessions();
    
    // Create initial session if none were loaded
    if (Sessions.Num() == 0)
    {
        NewSession();
        AddMessage(TEXT("System"), 
            TEXT("Welcome to UnrealOpenCode AI Assistant.\n\n")
            TEXT("Use ```code blocks``` for formatted code.\n")
            TEXT("Click the sidebar to switch between chat sessions."),
            false);
    }
    
    ChildSlot
    [
        SNew(SHorizontalBox)
        
        // Session sidebar (left)
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
            .BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f))
            .Visibility(this, &SUnrealOpenCodePanel::GetSessionSidebarVisibility)
            [
                SNew(SVerticalBox)
                
                // Sidebar header with New Chat button
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(8.0f, 4.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Sessions")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("+ New")))
                        .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                        .OnClicked_Lambda([this]()
                        {
                            NewSession();
                            return FReply::Handled();
                        })
                ]
                
                // Session list
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SAssignNew(SessionListView, SListView<TSharedPtr<FUEOCChatSession>>)
                    .ListItemsSource(&Sessions)
                    .OnGenerateRow(this, &SUnrealOpenCodePanel::OnGenerateSessionRow)
                    .OnSelectionChanged(this, &SUnrealOpenCodePanel::OnSessionSelected)
                    .SelectionMode(ESelectionMode::Single)
                ]
            ]
        ]
        
        // Main chat area (right)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNew(SVerticalBox)
            
            // Header bar
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("☰")))
                    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(FText::FromString(TEXT("Toggle Session Sidebar")))
                    .OnClicked_Lambda([this]() 
                    { 
                        ToggleSessionSidebar(); 
                        return FReply::Handled(); 
                    })
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("UnrealOpenCode AI Assistant")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SAssignNew(ConnectionStatusTextBlock, STextBlock)
                    .Text(this, &SUnrealOpenCodePanel::GetConnectionStatusText)
                    .ColorAndOpacity(this, &SUnrealOpenCodePanel::GetConnectionStatusColor)
                ]
            ]
            
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SSeparator)
            ]
            
            // Message list area
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(4.0f)
            [
                SAssignNew(MessageScrollBox, SScrollBox)
                + SScrollBox::Slot()
                [
                    SAssignNew(MessageListView, SListView<TSharedPtr<FUEOCChatMessage>>)
                    .ListItemsSource(&Messages)
                    .OnGenerateRow(this, &SUnrealOpenCodePanel::OnGenerateMessageRow)
                    .SelectionMode(ESelectionMode::None)
                ]
            ]
            
            // Context bar (shows connection status)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SSeparator)
            ]
            
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f, 2.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ContextBarTextBlock, STextBlock)
                    .Text(this, &SUnrealOpenCodePanel::GetContextBarText)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                ]
            ]
            
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SSeparator)
            ]
            
            // Input area
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SAssignNew(InputTextBox, SEditableTextBox)
                    .HintText(FText::FromString(TEXT("Type a message... (Enter to send)")))
                    .OnTextCommitted(this, &SUnrealOpenCodePanel::OnInputTextCommitted)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Send")))
                    .OnClicked(this, &SUnrealOpenCodePanel::OnSendMessage)
                ]
            ]
        ]
    ];
}

void SUnrealOpenCodePanel::AddMessage(const FString& Sender, const FString& Text, bool bIsUser)
{
    TSharedPtr<FUEOCChatMessage> NewMessage = MakeShareable(new FUEOCChatMessage{
        Sender,
        Text,
        FDateTime::UtcNow(),
        bIsUser
    });
    
    // Parse segments for code block detection
    NewMessage->ParseMessageSegments();
    
    Messages.Add(NewMessage);
    
    // Also add to current session
    if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
    {
        Sessions[CurrentSessionIndex]->Messages.Add(NewMessage);
        
        // Set session title from first user message if not already set
        if (bIsUser && Sessions[CurrentSessionIndex]->Title.IsEmpty())
        {
            FString TitleText = Text.Len() > 30 ? Text.Left(30) + TEXT("...") : Text;
            Sessions[CurrentSessionIndex]->Title = TitleText;
            if (SessionListView.IsValid())
            {
                SessionListView->RequestListRefresh();
            }
        }
    }
    
    if (MessageListView.IsValid())
    {
        MessageListView->RequestListRefresh();
    }
    
    ScrollToBottom();
    
    // Auto-save current session to disk
    SaveSession(CurrentSessionIndex);
}

void SUnrealOpenCodePanel::SetConnectionStatus(bool bConnected, const FString& StatusText)
{
    bIsConnected = bConnected;
    ConnectionStatusString = StatusText.IsEmpty() 
        ? (bConnected ? TEXT("Connected") : TEXT("Disconnected"))
        : StatusText;
}

void SUnrealOpenCodePanel::NewSession()
{
    TSharedPtr<FUEOCChatSession> NewSession = MakeShareable(new FUEOCChatSession());
    NewSession->SessionId = FGuid::NewGuid().ToString();
    NewSession->Title = TEXT("New Chat");
    NewSession->CreatedAt = FDateTime::UtcNow();
    NewSession->bIsActive = true;
    
    // Deactivate current session
    if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
    {
        Sessions[CurrentSessionIndex]->bIsActive = false;
    }
    
    Sessions.Add(NewSession);
    CurrentSessionIndex = Sessions.Num() - 1;
    
    // Clear current messages and load from new session (which is empty)
    Messages.Empty();
    
    if (MessageListView.IsValid())
    {
        MessageListView->RequestListRefresh();
    }
    
    if (SessionListView.IsValid())
    {
        SessionListView->RequestListRefresh();
        SessionListView->SetSelection(NewSession);
    }
}

void SUnrealOpenCodePanel::LoadSession(int32 Index)
{
    if (Index < 0 || Index >= Sessions.Num())
    {
        return;
    }
    
    // Deactivate current
    if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
    {
        Sessions[CurrentSessionIndex]->bIsActive = false;
    }
    
    // Activate new
    CurrentSessionIndex = Index;
    Sessions[CurrentSessionIndex]->bIsActive = true;
    
    // Load messages
    Messages = Sessions[CurrentSessionIndex]->Messages;
    
    if (MessageListView.IsValid())
    {
        MessageListView->RequestListRefresh();
    }
    
    if (SessionListView.IsValid())
    {
        SessionListView->RequestListRefresh();
    }
    
    ScrollToBottom();
}

void SUnrealOpenCodePanel::ToggleSessionSidebar()
{
    bSessionSidebarVisible = !bSessionSidebarVisible;
}

void SUnrealOpenCodePanel::SaveSession(int32 SessionIndex)
{
    if (SessionIndex < 0 || SessionIndex >= Sessions.Num())
    {
        return;
    }
    
    const TSharedPtr<FUEOCChatSession>& Session = Sessions[SessionIndex];
    
    // Ensure directory exists
    FString Dir = FPaths::ProjectDir() / TEXT("Saved/UnrealOpenCode/sessions");
    IFileManager::Get().MakeDirectory(*Dir, true);
    
    // Build messages JSON array
    FString MessagesJson = TEXT("[");
    for (int32 i = 0; i < Session->Messages.Num(); ++i)
    {
        const auto& Msg = Session->Messages[i];
        FString EscapedText = Msg->Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT("\\r"));
        FString EscapedSender = Msg->Sender.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
        MessagesJson += FString::Printf(TEXT("{\"sender\":\"%s\",\"text\":\"%s\",\"timestamp\":\"%s\",\"isUser\":%s}"),
            *EscapedSender, *EscapedText, *Msg->Timestamp.ToIso8601(), Msg->bIsUser ? TEXT("true") : TEXT("false"));
        if (i < Session->Messages.Num() - 1) MessagesJson += TEXT(",");
    }
    MessagesJson += TEXT("]");
    
    // Build full session JSON
    FString EscapedTitle = Session->Title.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
    FString JsonStr = FString::Printf(TEXT("{\"sessionId\":\"%s\",\"title\":\"%s\",\"createdAt\":\"%s\",\"messages\":%s}"),
        *Session->SessionId, *EscapedTitle, *Session->CreatedAt.ToIso8601(), *MessagesJson);
    
    // Write session file
    FString FilePath = Dir / FString::Printf(TEXT("session_%s.json"), *Session->SessionId);
    FFileHelper::SaveStringToFile(JsonStr, *FilePath);
    
    // Update index file
    SaveSessionIndex();
}

void SUnrealOpenCodePanel::SaveSessionIndex()
{
    FString Dir = FPaths::ProjectDir() / TEXT("Saved/UnrealOpenCode/sessions");
    IFileManager::Get().MakeDirectory(*Dir, true);
    
    FString IndexJson = TEXT("[");
    for (int32 i = 0; i < Sessions.Num(); ++i)
    {
        const auto& Session = Sessions[i];
        FString EscapedTitle = Session->Title.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
        IndexJson += FString::Printf(TEXT("{\"sessionId\":\"%s\",\"title\":\"%s\",\"createdAt\":\"%s\"}"),
            *Session->SessionId, *EscapedTitle, *Session->CreatedAt.ToIso8601());
        if (i < Sessions.Num() - 1) IndexJson += TEXT(",");
    }
    IndexJson += TEXT("]");
    
    FString FilePath = Dir / TEXT("sessions_index.json");
    FFileHelper::SaveStringToFile(IndexJson, *FilePath);
}

void SUnrealOpenCodePanel::LoadAllSessions()
{
    FString Dir = FPaths::ProjectDir() / TEXT("Saved/UnrealOpenCode/sessions");
    FString IndexPath = Dir / TEXT("sessions_index.json");
    
    if (!IFileManager::Get().FileExists(*IndexPath))
    {
        return;
    }
    
    FString IndexContent;
    if (!FFileHelper::LoadFileToString(IndexContent, *IndexPath))
    {
        return;
    }
    
    // Parse session IDs from index
    TArray<FString> SessionIds;
    {
        FString SearchKey = TEXT("\"sessionId\":\"");
        int32 SearchPos = 0;
        while (true)
        {
            int32 KeyPos = IndexContent.Find(SearchKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
            if (KeyPos == INDEX_NONE) break;
            
            int32 ValueStart = KeyPos + SearchKey.Len();
            int32 ValueEnd = IndexContent.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
            if (ValueEnd == INDEX_NONE) break;
            
            SessionIds.Add(IndexContent.Mid(ValueStart, ValueEnd - ValueStart));
            SearchPos = ValueEnd + 1;
        }
    }
    
    // Load each session file
    for (const FString& SessionId : SessionIds)
    {
        FString SessionPath = Dir / FString::Printf(TEXT("session_%s.json"), *SessionId);
        FString SessionContent;
        if (!FFileHelper::LoadFileToString(SessionContent, *SessionPath))
        {
            continue;
        }
        
        TSharedPtr<FUEOCChatSession> Session = MakeShareable(new FUEOCChatSession());
        Session->SessionId = SessionId;
        Session->Title = UEOCJsonHelper::ExtractStringValue(SessionContent, TEXT("title"));
        
        // Parse createdAt
        FString CreatedAtStr = UEOCJsonHelper::ExtractStringValue(SessionContent, TEXT("createdAt"));
        if (!CreatedAtStr.IsEmpty())
        {
            FDateTime::ParseIso8601(*CreatedAtStr, Session->CreatedAt);
        }
        
        // Parse messages array
        FString MsgArrayKey = TEXT("\"messages\":[");
        int32 MsgArrayStart = SessionContent.Find(MsgArrayKey, ESearchCase::CaseSensitive);
        if (MsgArrayStart != INDEX_NONE)
        {
            int32 ArrayContentStart = MsgArrayStart + MsgArrayKey.Len();
            
            // Find matching ] while tracking string boundaries
            int32 ArrPos = ArrayContentStart;
            int32 ArrDepth = 1;
            bool bInStr = false;
            while (ArrPos < SessionContent.Len() && ArrDepth > 0)
            {
                TCHAR Ch = SessionContent[ArrPos];
                if (bInStr)
                {
                    if (Ch == TEXT('\\')) { ArrPos++; }
                    else if (Ch == TEXT('"')) { bInStr = false; }
                }
                else
                {
                    if (Ch == TEXT('"')) bInStr = true;
                    else if (Ch == TEXT('[')) ArrDepth++;
                    else if (Ch == TEXT(']')) ArrDepth--;
                }
                if (ArrDepth > 0) ArrPos++;
            }
            
            FString MessagesStr = SessionContent.Mid(ArrayContentStart, ArrPos - ArrayContentStart);
            
            // Parse individual message objects
            int32 MsgSearchPos = 0;
            while (MsgSearchPos < MessagesStr.Len())
            {
                int32 ObjStart = MessagesStr.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, MsgSearchPos);
                if (ObjStart == INDEX_NONE) break;
                
                // Find matching } while tracking strings
                int32 ObjPos = ObjStart + 1;
                int32 ObjDepth = 1;
                bool bObjInStr = false;
                while (ObjPos < MessagesStr.Len() && ObjDepth > 0)
                {
                    TCHAR ObjCh = MessagesStr[ObjPos];
                    if (bObjInStr)
                    {
                        if (ObjCh == TEXT('\\')) { ObjPos++; }
                        else if (ObjCh == TEXT('"')) { bObjInStr = false; }
                    }
                    else
                    {
                        if (ObjCh == TEXT('"')) bObjInStr = true;
                        else if (ObjCh == TEXT('{')) ObjDepth++;
                        else if (ObjCh == TEXT('}')) ObjDepth--;
                    }
                    if (ObjDepth > 0) ObjPos++;
                }
                
                if (ObjDepth != 0) break;
                
                FString MsgObjStr = MessagesStr.Mid(ObjStart, ObjPos - ObjStart + 1);
                
                TSharedPtr<FUEOCChatMessage> Msg = MakeShareable(new FUEOCChatMessage());
                Msg->Sender = UEOCJsonHelper::ExtractStringValue(MsgObjStr, TEXT("sender"));
                Msg->Text = UEOCJsonHelper::ExtractStringValue(MsgObjStr, TEXT("text"));
                Msg->bIsUser = UEOCJsonHelper::ExtractBoolValue(MsgObjStr, TEXT("isUser"));
                
                FString TimestampStr = UEOCJsonHelper::ExtractStringValue(MsgObjStr, TEXT("timestamp"));
                if (!TimestampStr.IsEmpty())
                {
                    FDateTime::ParseIso8601(*TimestampStr, Msg->Timestamp);
                }
                
                Msg->ParseMessageSegments();
                Session->Messages.Add(Msg);
                
                MsgSearchPos = ObjPos + 1;
            }
        }
        
        Sessions.Add(Session);
    }
    
    // Sort by createdAt descending (newest first)
    Sessions.Sort([](const TSharedPtr<FUEOCChatSession>& A, const TSharedPtr<FUEOCChatSession>& B)
    {
        return A->CreatedAt > B->CreatedAt;
    });
    
    // Activate newest session
    if (Sessions.Num() > 0)
    {
        CurrentSessionIndex = 0;
        Sessions[0]->bIsActive = true;
        Messages = Sessions[0]->Messages;
    }
}

void SUnrealOpenCodePanel::DeleteSession(int32 Index)
{
    if (Index < 0 || Index >= Sessions.Num())
    {
        return;
    }
    
    // Delete session file from disk
    FString Dir = FPaths::ProjectDir() / TEXT("Saved/UnrealOpenCode/sessions");
    FString FilePath = Dir / FString::Printf(TEXT("session_%s.json"), *Sessions[Index]->SessionId);
    IFileManager::Get().Delete(*FilePath);
    
    bool bDeletedActive = (Index == CurrentSessionIndex);
    
    // Remove from array
    Sessions.RemoveAt(Index);
    
    if (Sessions.Num() == 0)
    {
        // No sessions left - create a fresh one
        CurrentSessionIndex = -1;
        NewSession();
        AddMessage(TEXT("System"), 
            TEXT("Welcome to UnrealOpenCode AI Assistant.\n\n")
            TEXT("Use ```code blocks``` for formatted code.\n")
            TEXT("Click the sidebar to switch between chat sessions."),
            false);
    }
    else if (bDeletedActive)
    {
        // Deleted the active session - load the nearest one
        CurrentSessionIndex = FMath::Min(Index, Sessions.Num() - 1);
        LoadSession(CurrentSessionIndex);
    }
    else if (Index < CurrentSessionIndex)
    {
        // Deleted before active - adjust index
        CurrentSessionIndex--;
    }
    
    // Save updated index
    SaveSessionIndex();
    
    // Refresh sidebar
    if (SessionListView.IsValid())
    {
        SessionListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SUnrealOpenCodePanel::OnGenerateSessionRow(
    TSharedPtr<FUEOCChatSession> Session,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    FLinearColor BackgroundColor = Session->bIsActive 
        ? FLinearColor(0.2f, 0.4f, 0.8f, 0.3f) 
        : FLinearColor(0.15f, 0.15f, 0.15f, 0.5f);
    
    return SNew(STableRow<TSharedPtr<FUEOCChatSession>>, OwnerTable)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
        .BackgroundColor(BackgroundColor)
        .Padding(8.0f, 6.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Session->Title))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    .ColorAndOpacity(Session->bIsActive ? FLinearColor(1.0f, 1.0f, 1.0f) : FLinearColor(0.8f, 0.8f, 0.8f))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Session->CreatedAt.ToString(TEXT("%m/%d %H:%M"))))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("\u2715")))
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(FText::FromString(TEXT("Delete session")))
                .OnClicked_Lambda([this, SessionId = Session->SessionId]()
                {
                    for (int32 i = 0; i < Sessions.Num(); ++i)
                    {
                        if (Sessions[i]->SessionId == SessionId)
                        {
                            DeleteSession(i);
                            break;
                        }
                    }
                    return FReply::Handled();
                })
            ]
        ]
    ];
}

void SUnrealOpenCodePanel::OnSessionSelected(TSharedPtr<FUEOCChatSession> Session, ESelectInfo::Type SelectInfo)
{
    if (!Session.IsValid())
    {
        return;
    }
    
    // Find session index
    for (int32 i = 0; i < Sessions.Num(); ++i)
    {
        if (Sessions[i]->SessionId == Session->SessionId)
        {
            if (i != CurrentSessionIndex)
            {
                LoadSession(i);
            }
            break;
        }
    }
}

FReply SUnrealOpenCodePanel::OnSendMessage()
{
    if (InputTextBox.IsValid())
    {
        FString InputText = InputTextBox->GetText().ToString();
        if (!InputText.IsEmpty())
        {
            AddMessage(TEXT("User"), InputText, true);
            InputTextBox->SetText(FText::GetEmpty());
            
            // Simulate AI response (placeholder)
            // In V2, this would actually send to oh-my-opencode
            FTimerHandle DummyHandle;
            GEditor->GetTimerManager()->SetTimer(DummyHandle, [this]()
            {
                AddMessage(TEXT("Assistant"), 
                    TEXT("This is a placeholder response. AI integration coming in Task 20+!\n\n")
                    TEXT("Here's a sample code block:\n```cpp\nvoid HelloWorld()\n{\n    UE_LOG(LogTemp, Log, TEXT(\"Hello!\"));\n}\n```\n\n")
                    TEXT("You can copy the code or insert it into a file."),
                    false);
            }, 1.0f, false);
        }
    }
    return FReply::Handled();
}

void SUnrealOpenCodePanel::OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::OnEnter)
    {
        OnSendMessage();
    }
}

TSharedRef<ITableRow> SUnrealOpenCodePanel::OnGenerateMessageRow(
    TSharedPtr<FUEOCChatMessage> Message,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor UserColor = FLinearColor(0.2f, 0.4f, 0.8f);
    const FLinearColor AssistantColor = FLinearColor(0.4f, 0.4f, 0.4f);
    const FLinearColor BorderColor = Message->bIsUser ? UserColor : AssistantColor;
    const FLinearColor BackgroundColor = Message->bIsUser 
        ? FLinearColor(0.15f, 0.25f, 0.5f, 0.3f) 
        : FLinearColor(0.3f, 0.3f, 0.3f, 0.2f);
    
    return SNew(STableRow<TSharedPtr<FUEOCChatMessage>>, OwnerTable)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("BoxBorder"))
        .BorderBackgroundColor(BorderColor)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            
            // Header row: Sender + Copy button
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Message->Sender))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    .ColorAndOpacity(BorderColor)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("📋")))
                    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(FText::FromString(TEXT("Copy message to clipboard")))
                    .OnClicked_Lambda([this, Message]() 
                    { 
                        return OnCopyMessage(Message->Text); 
                    })
                ]
            ]
            
            // Message content (handles segments)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 0.0f)
            [
                BuildMessageContent(Message)
            ]
        ]
    ];
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildMessageContent(TSharedPtr<FUEOCChatMessage> Message)
{
    if (Message->Segments.Num() == 0)
    {
        // No segments parsed, show raw text
        return SNew(STextBlock)
            .Text(FText::FromString(Message->Text))
            .AutoWrapText(true);
    }
    
    TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox);
    
    for (const FUEOCChatMessageSegment& Segment : Message->Segments)
    {
        if (Segment.bIsCode)
        {
            // Code block with dark background and buttons
            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                BuildCodeBlockWidget(Segment.Text)
            ];
        }
        else
        {
            // Plain text
            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Segment.Text))
                .AutoWrapText(true)
            ];
        }
    }
    
    return ContentBox;
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildCodeBlockWidget(const FString& CodeText)
{
    return SNew(SBorder)
    .BorderImage(FCoreStyle::Get().GetBrush("BoxBorder"))
    .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f))
    .BackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f))
    .Padding(8.0f)
    [
        SNew(SVerticalBox)
        
        // Code text (scrollable horizontally if needed)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SScrollBox)
            .Orientation(EOrientation::Orient_Horizontal)
            + SScrollBox::Slot()
            [
                SNew(STextBlock)
                .Text(FText::FromString(CodeText))
                .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
            ]
        ]
        
        // Action buttons
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            []
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Copy")))
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(FText::FromString(TEXT("Copy code to clipboard")))
                .OnClicked_Lambda([this, CodeText]() 
                { 
                    return OnCopyCode(CodeText); 
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Insert to File...")))
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(FText::FromString(TEXT("Save code to a file")))
                .OnClicked_Lambda([this, CodeText]() 
                { 
                    return OnInsertCode(CodeText); 
                })
            ]
        ]
    ];
}

void SUnrealOpenCodePanel::ScrollToBottom()
{
    if (MessageScrollBox.IsValid())
    {
        MessageScrollBox->ScrollToEnd();
    }
}

FReply SUnrealOpenCodePanel::OnCopyMessage(const FString& MessageText)
{
    FPlatformApplicationMisc::ClipboardCopy(*MessageText);
    return FReply::Handled();
}

FReply SUnrealOpenCodePanel::OnCopyCode(const FString& CodeText)
{
    FPlatformApplicationMisc::ClipboardCopy(*CodeText);
    return FReply::Handled();
}

FReply SUnrealOpenCodePanel::OnInsertCode(const FString& CodeText)
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OutFilenames;
        const bool bResult = DesktopPlatform->SaveFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("Save Code File"),
            FPaths::ProjectDir() / TEXT("Source"),
            TEXT("NewFile"),
            TEXT("C++ Header (*.h)|*.h|C++ Source (*.cpp)|*.cpp|All Files (*.*)|*.*"),
            EFileDialogFlags::None,
            OutFilenames
        );
        
        if (bResult && OutFilenames.Num() > 0)
        {
            FFileHelper::SaveStringToFile(CodeText, *OutFilenames[0]);
        }
    }
    return FReply::Handled();
}

EVisibility SUnrealOpenCodePanel::GetSessionSidebarVisibility() const
{
    return bSessionSidebarVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SUnrealOpenCodePanel::GetConnectionStatusText() const
{
    FString Status = bIsConnected ? TEXT("● Connected") : TEXT("● Disconnected");
    return FText::FromString(Status);
}

FSlateColor SUnrealOpenCodePanel::GetConnectionStatusColor() const
{
    return bIsConnected ? FLinearColor(0.3f, 1.0f, 0.3f) : FLinearColor(1.0f, 0.3f, 0.3f);
}

FText SUnrealOpenCodePanel::GetContextBarText() const
{
    FString ContextText = FString::Printf(TEXT("Status: %s | Session: %d/%d"), 
        *ConnectionStatusString,
        CurrentSessionIndex + 1,
        Sessions.Num());
    return FText::FromString(ContextText);
}
