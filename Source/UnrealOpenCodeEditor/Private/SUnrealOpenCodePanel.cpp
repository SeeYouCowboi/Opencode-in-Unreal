// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUnrealOpenCodePanel.h"
#include "Editor.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
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
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Containers/Ticker.h"
#include "Algo/RemoveIf.h"

/** Map an agent name to its accent color for UI color-coding */
static FLinearColor GetAgentAccentColor(const FString& AgentName)
{
    if (AgentName.Contains(TEXT("Sisyphus")))   return FLinearColor(0.00f, 0.80f, 0.84f, 1.0f); // cyan
    if (AgentName.Contains(TEXT("Hephaestus"))) return FLinearColor(0.95f, 0.52f, 0.08f, 1.0f); // orange
    if (AgentName.Contains(TEXT("Prometheus"))) return FLinearColor(0.65f, 0.28f, 0.95f, 1.0f); // purple
    if (AgentName.Contains(TEXT("Atlas")))      return FLinearColor(0.20f, 0.55f, 1.00f, 1.0f); // blue
    if (AgentName.Contains(TEXT("Metis")))      return FLinearColor(0.20f, 0.85f, 0.40f, 1.0f); // green
    if (AgentName.Contains(TEXT("Momus")))      return FLinearColor(1.00f, 0.82f, 0.18f, 1.0f); // yellow
    return FLinearColor(0.35f, 0.55f, 0.85f, 1.0f); // default slate blue
}
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
        
        // If entering a code block, skip language identifier (e.g. ```cpp)
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
    // Read opencode HTTP port from DefaultEngine.ini
    GConfig->GetInt(TEXT("UnrealOpenCode"), TEXT("OpenCodePort"), OpenCodePort, GEngineIni);
    if (OpenCodePort <= 0) OpenCodePort = 4096;

    // Load existing sessions from disk
    LoadAllSessions();

    // Eagerly fetch available agents so the combo box populates immediately
    FetchAvailableAgents();
    
    // Create initial session if none were loaded
    if (Sessions.Num() == 0)
    {
        NewSession();
    }
    
    // Colors
    const FLinearColor DarkBackground   (0.07f, 0.07f, 0.07f, 1.0f);
    const FLinearColor SidebarBackground(0.05f, 0.05f, 0.05f, 1.0f);
    const FLinearColor TopBarBackground (0.09f, 0.09f, 0.09f, 1.0f);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(DarkBackground)
        .Padding(0.0f)
        [
            SNew(SHorizontalBox)

            // ─── LEFT SIDEBAR ────────────────────────────────────────────────
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBox)
                .WidthOverride(155.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .BorderBackgroundColor(SidebarBackground)
                    .Padding(0.0f)
                    [
                        SNew(SVerticalBox)

                        // Sidebar header: "Chats" label + [+] new button
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                            .BorderBackgroundColor(TopBarBackground)
                            .Padding(8.0f, 6.0f)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot()
                                .FillWidth(1.0f)
                                .VAlign(VAlign_Center)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Chats")))
                                    .Font(FAppStyle::GetFontStyle("BoldFont"))
                                ]
                                + SHorizontalBox::Slot()
                                .AutoWidth()
                                [
                                    SNew(SButton)
                                    .Text(FText::FromString(TEXT("+")))
                                    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                    .ToolTipText(FText::FromString(TEXT("New Chat")))
                                    .OnClicked_Lambda([this]()
                                    {
                                        NewSession();
                                        return FReply::Handled();
                                    })
                                ]
                            ]
                        ]

                        // Session list
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(SessionListScrollBox, SScrollBox)
                        ]
                    ]
                ]
            ]

            // Thin vertical divider
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.04f, 1.0f))
                .Padding(1.0f, 0.0f)
            ]

            // ─── MAIN CONTENT ─────────────────────────────────────────────────
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SVerticalBox)

                // Top bar: session title (center) + connection dot (right)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .BorderBackgroundColor(TopBarBackground)
                    .Padding(8.0f, 6.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .HAlign(HAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(this, &SUnrealOpenCodePanel::GetCurrentSessionTitle)
                            .Font(FAppStyle::GetFontStyle("BoldFont"))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        [
                            SAssignNew(ConnectionStatusTextBlock, STextBlock)
                            .Text(this, &SUnrealOpenCodePanel::GetConnectionStatusText)
                            .ColorAndOpacity(this, &SUnrealOpenCodePanel::GetConnectionStatusColor)
                            .Font(FAppStyle::GetFontStyle("NormalFont"))
                        ]
                    ]
                ]

                // Content area (Welcome or Chat switcher)
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(0.0f)
                [
                    SAssignNew(ContentSwitcher, SWidgetSwitcher)
                    .WidgetIndex(this, &SUnrealOpenCodePanel::GetContentSwitcherIndex)

                    // Slot 0: Welcome state
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                        .BorderBackgroundColor(DarkBackground)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .FillHeight(1.0f)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot()
                                .FillWidth(1.0f)
                                [
                                    SNew(SVerticalBox)
                                    + SVerticalBox::Slot().FillHeight(1.0f)[ SNullWidget::NullWidget ]
                                    + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .HAlign(HAlign_Center)
                                    [
                                        SNew(STextBlock)
                                        .Text(FText::FromString(TEXT("UnrealOpenCode")))
                                        .Font(FAppStyle::GetFontStyle("HugeBoldFont"))
                                    ]
                                    + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                                    .HAlign(HAlign_Center)
                                    [
                                        SNew(STextBlock)
                                        .Text(FText::FromString(TEXT("AI for Unreal Engine Development")))
                                        .Font(FAppStyle::GetFontStyle("SmallFont"))
                                        .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
                                    ]
                                    + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .Padding(0.0f, 32.0f, 0.0f, 0.0f)
                                    .HAlign(HAlign_Center)
                                    [
                                        SNew(SVerticalBox)
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
                                        [ SNew(STextBlock).Text(FText::FromString(TEXT("\x2022 Inspect C++ class hierarchies and Blueprints"))).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f,1.0f)) ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
                                        [ SNew(STextBlock).Text(FText::FromString(TEXT("\x2022 Generate and review code with confirmation"))).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f,1.0f)) ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
                                        [ SNew(STextBlock).Text(FText::FromString(TEXT("\x2022 Query build logs and compilation status"))).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f,1.0f)) ]
                                    ]
                                    + SVerticalBox::Slot().FillHeight(1.0f)[ SNullWidget::NullWidget ]
                                ]
                            ]
                        ]
                    ]

                    // Slot 1: Chat state (messages)
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                        .BorderBackgroundColor(DarkBackground)
                        .Padding(12.0f, 8.0f)
                        [
                            SAssignNew(MessageScrollBox, SScrollBox)
                            .ScrollBarThickness(FVector2D(8.0f, 8.0f))
                        ]
                    ]
                ]

                // Separator above input
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.12f, 1.0f))
                    .Padding(0.0f, 1.0f)
                ]

                // Input bar
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(8.0f, 8.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SAssignNew(AgentComboBox, SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&AvailableAgentOptions)
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewItem, ESelectInfo::Type)
                        {
                            if (NewItem.IsValid()) { SelectedAgent = NewItem; }
                        })
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
                        {
                            return SNew(STextBlock)
                                .Text(FText::FromString(Item.IsValid() ? *Item : TEXT("Default")))
                                .Font(FAppStyle::GetFontStyle("SmallFont"));
                        })
                        .InitiallySelectedItem(SelectedAgent)
                        [
                            SNew(STextBlock)
                            .Text(this, &SUnrealOpenCodePanel::GetSelectedAgentText)
                            .Font(FAppStyle::GetFontStyle("SmallFont"))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [
                        SAssignNew(InputTextBox, SEditableTextBox)
                        .HintText(FText::FromString(TEXT("Ask anything about Unreal Engine...")))
                        .OnTextCommitted(this, &SUnrealOpenCodePanel::OnInputTextCommitted)
                        .BackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("\x2192")))
                        .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                        .ToolTipText(FText::FromString(TEXT("Send message")))
                        .OnClicked(this, &SUnrealOpenCodePanel::OnSendMessage)
                    ]
                ]
            ]
        ]
    ];

    // Populate the sidebar with existing sessions
    RebuildSessionList();
}

void SUnrealOpenCodePanel::AddMessage(const FString& Sender, const FString& Text, bool bIsUser, const FString& InAgentName)
{
    TSharedPtr<FUEOCChatMessage> NewMessage = MakeShareable(new FUEOCChatMessage{
        Sender,
        Text,
        FDateTime::UtcNow(),
        bIsUser,
        InAgentName
    });
    
    // Parse segments for code block detection
    NewMessage->ParseMessageSegments();
    
    Messages.Add(NewMessage);
    
    // Also add to current session
    if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
    {
        Sessions[CurrentSessionIndex]->Messages.Add(NewMessage);
        
        // Set session title from first user message if not already set
        if (bIsUser && (Sessions[CurrentSessionIndex]->Title.IsEmpty() || Sessions[CurrentSessionIndex]->Title == TEXT("New Chat")))
        {
            FString TitleText = Text.Len() > 30 ? Text.Left(30) + TEXT("...") : Text;
            Sessions[CurrentSessionIndex]->Title = TitleText;
        }
    }
    
    // Rebuild message area (switches from welcome to chat if needed)
    RebuildMessageArea();
    
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
    
    // Rebuild UI (will show welcome state)
    RebuildMessageArea();
}

void SUnrealOpenCodePanel::LoadSession(int32 Index)
{
    StopLivePolling(); // cancel any in-progress streaming when switching sessions
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
    
    // Rebuild message area
    RebuildMessageArea();
}

void SUnrealOpenCodePanel::RebuildMessageArea()
{
    // Update the widget switcher index (welcome vs chat)
    if (ContentSwitcher.IsValid())
    {
        ContentSwitcher->SetActiveWidgetIndex(GetContentSwitcherIndex());
    }
    
    // If we have messages, populate the scroll box
    if (Messages.Num() > 0 && MessageScrollBox.IsValid())
    {
        MessageScrollBox->ClearChildren();
        
        for (const auto& Msg : Messages)
        {
            MessageScrollBox->AddSlot()
            .Padding(0.0f, 4.0f)
            [
                BuildMessageWidget(Msg)
            ];
        }
        
        ScrollToBottom();
    }

    // Keep sidebar in sync (title may update on first message)
    RebuildSessionList();
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildMessageWidget(TSharedPtr<FUEOCChatMessage> Message)
{
    const FLinearColor UserBgColor(0.06f, 0.10f, 0.18f, 1.0f);
    const FLinearColor AIBgColor  (0.10f, 0.10f, 0.10f, 1.0f);
    const FLinearColor UserAccent (0.00f, 0.78f, 0.80f, 1.0f); // teal — user messages

    const FLinearColor BgColor     = Message->bIsUser ? UserBgColor : AIBgColor;
    const FLinearColor AccentColor = Message->bIsUser
        ? UserAccent
        : GetAgentAccentColor(Message->AgentName);

    // Label shown below AI content: agent name or fallback
    const FString LabelText = Message->bIsUser ? TEXT("")
        : (Message->AgentName.IsEmpty() ? TEXT("UnrealOpenCode") : Message->AgentName);

    // --- Outer container ---
    auto ContentWidget =
        SNew(SVerticalBox)
        // Main text / code content
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            BuildMessageContent(Message)
        ]
        // Agent name label below content (AI only)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(LabelText))
            .Font(FAppStyle::GetFontStyle("SmallFont"))
            .ColorAndOpacity(FSlateColor(AccentColor))
            .Visibility(Message->bIsUser ? EVisibility::Collapsed : EVisibility::Visible)
        ];

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(BgColor)
        .Padding(0.0f)
        [
            SNew(SHorizontalBox)
            // Colored left accent strip (3 px)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBox)
                .WidthOverride(3.0f)
                [
                    SNew(SColorBlock)
                    .Color(AccentColor)
                ]
            ]
            // Message content with padding
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(12.0f, 8.0f)
            [
                ContentWidget
            ]
        ];
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
    StopLivePolling(); // cancel any in-progress streaming
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
    
    // Save updated index and refresh sidebar
    SaveSessionIndex();
    RebuildSessionList();
}

FReply SUnrealOpenCodePanel::OnSendMessage()
{
    if (!InputTextBox.IsValid()) return FReply::Handled();

    FString InputText = InputTextBox->GetText().ToString().TrimStartAndEnd();
    if (InputText.IsEmpty()) return FReply::Handled();

    AddMessage(TEXT("User"), InputText, true);
    InputTextBox->SetText(FText::GetEmpty());

    if (OpenCodeSessionId.IsEmpty())
    {
        CreateOpenCodeSession([this, InputText]()
        {
            SendMessageToOpenCode(InputText);
        });
    }
    else
    {
        SendMessageToOpenCode(InputText);
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

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildMessageContent(TSharedPtr<FUEOCChatMessage> Message)
{
    // User messages: always plain text, no markdown processing
    if (Message->bIsUser)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Message->Text))
            .AutoWrapText(true)
            .ColorAndOpacity(FLinearColor(0.88f, 0.88f, 0.88f, 1.0f));
    }

    // AI messages: full markdown rendering
    if (Message->Segments.Num() == 0)
    {
        return BuildMarkdownWidget(ParseMarkdownLines(Message->Text));
    }

    TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox);
    for (const FUEOCChatMessageSegment& Segment : Message->Segments)
    {
        if (Segment.bIsCode)
        {
            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                BuildCodeBlockWidget(Segment.Text)
            ];
        }
        else
        {
            ContentBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 2.0f)
            [
                BuildMarkdownWidget(ParseMarkdownLines(Segment.Text))
            ];
        }
    }
    return ContentBox;
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildCodeBlockWidget(const FString& CodeText)
{
    return SNew(SBorder)
    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
    .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
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
                .Font(FAppStyle::GetFontStyle("MonoFont"))
                .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
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
            [SNullWidget::NullWidget]
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

FText SUnrealOpenCodePanel::GetConnectionStatusText() const
{
    return FText::FromString(TEXT("\x25CF"));
}

FSlateColor SUnrealOpenCodePanel::GetConnectionStatusColor() const
{
    return bIsConnected ? FLinearColor(0.2f, 0.8f, 0.2f, 1.0f) : FLinearColor(0.9f, 0.2f, 0.2f, 1.0f);
}

FText SUnrealOpenCodePanel::GetCurrentSessionTitle() const
{
    if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
    {
        return FText::FromString(Sessions[CurrentSessionIndex]->Title);
    }
    return FText::FromString(TEXT("UnrealOpenCode"));
}

// ---------------------------------------------------------------------------
// opencode HTTP client
// ---------------------------------------------------------------------------

void SUnrealOpenCodePanel::CreateOpenCodeSession(TFunction<void()> OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(FString::Printf(TEXT("http://localhost:%d/session"), OpenCodePort));
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetContentAsString(TEXT("{\"title\":\"UE Chat\"}"));

    Req->OnProcessRequestComplete().BindLambda(
        [this, OnComplete](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
        {
            if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
            {
                OpenCodeSessionId = UEOCJsonHelper::ExtractStringValue(Resp->GetContentAsString(), TEXT("id"));
                // Store the opencode session ID on the local session for title syncing
                if (CurrentSessionIndex >= 0 && CurrentSessionIndex < Sessions.Num())
                {
                    Sessions[CurrentSessionIndex]->OpenCodeSessionId = OpenCodeSessionId;
                }
                SetConnectionStatus(true);
                FetchAvailableAgents();
                if (OnComplete) OnComplete();
            }
            else
            {
                SetConnectionStatus(false, TEXT("Disconnected"));
                AddMessage(TEXT("System"),
                    FString::Printf(TEXT("Cannot reach opencode server on port %d.\n\nStart it with:\n  opencode serve --port %d"),
                    OpenCodePort, OpenCodePort),
                    false);
            }
        });

    PendingHttpRequest = Req;
    Req->ProcessRequest();
}

void SUnrealOpenCodePanel::SendMessageToOpenCode(const FString& UserText)
{
    // Reset live-polling state for new message
    StopLivePolling();
    LiveToolParts.Empty();
    ShownTextPartIds.Empty();
    KnownPartIds.Empty();
    LiveText.Empty();

    // Add a "thinking" placeholder
    AddMessage(TEXT("UnrealOpenCode"), TEXT("..."), false);
    ThinkingMessageIndex = Messages.Num() - 1;

    // Escape user text for JSON
    FString Escaped = UserText
        .Replace(TEXT("\\"), TEXT("\\\\"))
        .Replace(TEXT("\""), TEXT("\\\""))
        .Replace(TEXT("\n"), TEXT("\\n"))
        .Replace(TEXT("\r"), TEXT(""));

    FString AgentStr = SelectedAgent.IsValid() ? *SelectedAgent : TEXT("");
    FString Body;
    if (!AgentStr.IsEmpty())
    {
        Body = FString::Printf(
            TEXT("{\"parts\":[{\"type\":\"text\",\"text\":\"%s\"}],\"agent\":\"%s\"}"),
            *Escaped, *AgentStr);
    }
    else
    {
        Body = FString::Printf(
            TEXT("{\"parts\":[{\"type\":\"text\",\"text\":\"%s\"}]}"), *Escaped);
    }

    // ---- Step 1: Pre-flight GET to snapshot existing part IDs ----
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> PreflightReq = FHttpModule::Get().CreateRequest();
    PreflightReq->SetURL(FString::Printf(TEXT("http://localhost:%d/session/%s/message"),
        OpenCodePort, *OpenCodeSessionId));
    PreflightReq->SetVerb(TEXT("GET"));
    PreflightReq->SetHeader(TEXT("Accept"), TEXT("application/json"));
    PreflightReq->SetTimeout(10.0f);

    int32 SessionToUpdate = CurrentSessionIndex;
    PreflightReq->OnProcessRequestComplete().BindLambda(
        [this, Body, AgentStr, SessionToUpdate](FHttpRequestPtr, FHttpResponsePtr PreflightResp, bool bPreOk)
        {
            // Snapshot existing part IDs (ignore failures gracefully)
            if (bPreOk && PreflightResp.IsValid() && PreflightResp->GetResponseCode() == 200)
            {
                SnapshotExistingPartIds(PreflightResp->GetContentAsString());
            }

            // ---- Step 2: Fire the actual POST ----
            TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
            Req->SetURL(FString::Printf(TEXT("http://localhost:%d/session/%s/message"),
                OpenCodePort, *OpenCodeSessionId));
            Req->SetVerb(TEXT("POST"));
            Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
            Req->SetContentAsString(Body);
            Req->SetTimeout(180.0f);         // total cap: 3 minutes
            Req->SetActivityTimeout(0.0f);   // disable 30s no-data timeout (MCP tool calls cause silent gaps)

            Req->OnProcessRequestComplete().BindLambda(
                [this, AgentStr, SessionToUpdate](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
                {
                    // Stop polling — POST is done
                    StopLivePolling();

                    // Remove thinking placeholder
                    if (ThinkingMessageIndex >= 0 && ThinkingMessageIndex < Messages.Num())
                    {
                        Messages.RemoveAt(ThinkingMessageIndex);
                        ThinkingMessageIndex = -1;
                    }

                    if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
                    {
                        FString AiText = ExtractAiResponseText(Resp->GetContentAsString());
                        if (AiText.IsEmpty()) AiText = TEXT("(empty response)");
                        AddMessage(TEXT("UnrealOpenCode"), AiText, false, AgentStr);
                        FetchAndUpdateSessionTitle(SessionToUpdate);
                    }
                    else
                    {
                        int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
                        AddMessage(TEXT("System"),
                            FString::Printf(TEXT("Request failed (HTTP %d). Check opencode server."), Code),
                            false);
                    }
                });

            PendingHttpRequest = Req;
            Req->ProcessRequest();

            // ---- Step 3: Start polling while POST is in flight ----
            StartLivePolling();
        });

    PreflightReq->ProcessRequest();
}

FString SUnrealOpenCodePanel::ExtractAiResponseText(const FString& ResponseJson)
{
    // Response shape: {"info":{...},"parts":[{"type":"text","text":"..."},...] }
    // Collect all text parts
    FString Result;
    int32 SearchPos = 0;
    const FString TypeKey  = TEXT("\"type\":\"text\"");
    const FString TextKey  = TEXT("\"text\":\"");

    while (true)
    {
        // Find next text-type part
        int32 TypePos = ResponseJson.Find(TypeKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
        if (TypePos == INDEX_NONE) break;

        // Find "text":" after the type marker (within ~200 chars)
        int32 TextPos = ResponseJson.Find(TextKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, TypePos);
        if (TextPos == INDEX_NONE || TextPos - TypePos > 200) { SearchPos = TypePos + 1; continue; }

        int32 ValueStart = TextPos + TextKey.Len();
        // Read until unescaped closing quote
        FString Chunk;
        int32 Pos = ValueStart;
        while (Pos < ResponseJson.Len())
        {
            TCHAR Ch = ResponseJson[Pos];
            if (Ch == TEXT('\\') && Pos + 1 < ResponseJson.Len())
            {
                TCHAR Next = ResponseJson[Pos + 1];
                if (Next == TEXT('n'))       { Chunk += TEXT('\n'); Pos += 2; continue; }
                else if (Next == TEXT('r')) { Pos += 2; continue; }
                else if (Next == TEXT('t')) { Chunk += TEXT('\t'); Pos += 2; continue; }
                else                        { Chunk += Next;         Pos += 2; continue; }
            }
            if (Ch == TEXT('"')) break;
            Chunk += Ch;
            ++Pos;
        }

        if (!Chunk.IsEmpty())
        {
            if (!Result.IsEmpty()) Result += TEXT("\n");
            Result += Chunk;
        }
        SearchPos = Pos + 1;
    }
    return Result;
}

FText SUnrealOpenCodePanel::GetSelectedAgentText() const
{
    if (SelectedAgent.IsValid() && !SelectedAgent->IsEmpty())
    {
        return FText::FromString(*SelectedAgent);
    }
    return FText::FromString(TEXT("Default"));
}

void SUnrealOpenCodePanel::FetchAvailableAgents()
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(FString::Printf(TEXT("http://localhost:%d/agent"), OpenCodePort));
    Req->SetVerb(TEXT("GET"));
    Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Req->SetTimeout(10.0f);

    Req->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
        {
            if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;

            const FString Json = Resp->GetContentAsString();

            // Parse Agent[] - only include named primary agents
            // (agents whose name contains parentheses, e.g. "Sisyphus (Ultraworker)")
            AvailableAgentOptions.Empty();
            const FString NameKey = TEXT("\"name\":\"");
            int32 SearchPos = 0;
            while (true)
            {
                int32 KeyPos = Json.Find(NameKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
                if (KeyPos == INDEX_NONE) break;

                int32 ValueStart = KeyPos + NameKey.Len();
                int32 ValueEnd = Json.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
                if (ValueEnd == INDEX_NONE) break;

                FString AgentName = Json.Mid(ValueStart, ValueEnd - ValueStart);
                // Only keep agents whose name has a parenthesised subtitle
                const bool bHasParens = AgentName.Contains(TEXT("("));
                if (!AgentName.IsEmpty() && bHasParens)
                {
                    AvailableAgentOptions.Add(MakeShareable(new FString(AgentName)));
                }
                SearchPos = ValueEnd + 1;
            }

            // Pick the first agent as default if none selected yet
            if (AvailableAgentOptions.Num() > 0 && !SelectedAgent.IsValid())
            {
                SelectedAgent = AvailableAgentOptions[0];
            }

            if (AgentComboBox.IsValid())
            {
                AgentComboBox->RefreshOptions();
                AgentComboBox->SetSelectedItem(SelectedAgent);
            }
        });

    Req->ProcessRequest();
}

void SUnrealOpenCodePanel::RebuildSessionList()
{
    if (!SessionListScrollBox.IsValid()) return;

    SessionListScrollBox->ClearChildren();
    for (int32 i = 0; i < Sessions.Num(); ++i)
    {
        SessionListScrollBox->AddSlot()
        .Padding(0.0f, 1.0f)
        [
            BuildSessionItem(i)
        ];
    }
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildSessionItem(int32 Index)
{
    if (Index < 0 || Index >= Sessions.Num())
    {
        return SNullWidget::NullWidget;
    }

    const bool bIsActive = (Index == CurrentSessionIndex);
    const FLinearColor ActiveBg  (0.10f, 0.22f, 0.42f, 1.0f);
    const FLinearColor InactiveBg(0.00f, 0.00f, 0.00f, 0.0f);
    const FLinearColor ActiveText   = FLinearColor::White;
    const FLinearColor InactiveText (0.65f, 0.65f, 0.65f, 1.0f);

    FString Title = Sessions[Index]->Title;
    if (Title.IsEmpty()) Title = TEXT("New Chat");
    if (Title.Len() > 16) Title = Title.Left(16) + TEXT("...");

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(bIsActive ? ActiveBg : InactiveBg)
        .Padding(0.0f)
        [
            SNew(SHorizontalBox)
            // Title button
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ContentPadding(FMargin(8.0f, 5.0f))
                .HAlign(HAlign_Left)
                .ToolTipText(FText::FromString(Sessions[Index]->Title))
                .OnClicked_Lambda([this, Index]()
                {
                    LoadSession(Index);
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Title))
                    .Font(FAppStyle::GetFontStyle("SmallFont"))
                    .ColorAndOpacity(bIsActive ? ActiveText : InactiveText)
                ]
            ]
            // Delete button
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("\u00D7")))
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ContentPadding(FMargin(4.0f, 4.0f))
                .ToolTipText(FText::FromString(TEXT("Delete this chat")))
                .OnClicked_Lambda([this, Index]()
                {
                    DeleteSession(Index);
                    return FReply::Handled();
                })
            ]
        ];
}

// ---------------------------------------------------------------------------
// Session title sync from opencode
// ---------------------------------------------------------------------------

void SUnrealOpenCodePanel::FetchAndUpdateSessionTitle(int32 SessionIndex)
{
    if (SessionIndex < 0 || SessionIndex >= Sessions.Num()) return;
    const FString OCSessionId = Sessions[SessionIndex]->OpenCodeSessionId;
    if (OCSessionId.IsEmpty()) return;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(FString::Printf(TEXT("http://localhost:%d/session/%s"), OpenCodePort, *OCSessionId));
    Req->SetVerb(TEXT("GET"));
    Req->SetTimeout(10.0f);

    Req->OnProcessRequestComplete().BindLambda(
        [this, SessionIndex, OCSessionId](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
        {
            if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
            if (SessionIndex >= Sessions.Num()) return;
            // Guard against stale callbacks if the session was replaced
            if (Sessions[SessionIndex]->OpenCodeSessionId != OCSessionId) return;

            FString NewTitle = UEOCJsonHelper::ExtractStringValue(Resp->GetContentAsString(), TEXT("title"));

            // Only update if opencode generated a meaningful title
            if (NewTitle.IsEmpty() || NewTitle == TEXT("UE Chat")) return;

            Sessions[SessionIndex]->Title = NewTitle;
            SaveSessionIndex();
            RebuildSessionList();
        });

    Req->ProcessRequest();
}

// ---------------------------------------------------------------------------
// Live polling — real-time streaming display
// ---------------------------------------------------------------------------

void SUnrealOpenCodePanel::StartLivePolling()
{
    bPollingActive = true;
    bPollingRequestInFlight = false;

    TWeakPtr<SUnrealOpenCodePanel> PollWeakThis = SharedThis(this);
    PollingTickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([PollWeakThis](float) -> bool
        {
            TSharedPtr<SUnrealOpenCodePanel> Panel = PollWeakThis.Pin();
            if (!Panel.IsValid()) return false;
            return Panel->OnLivePollingTick(0.0f);
        }), 1.5f);
}

void SUnrealOpenCodePanel::StopLivePolling()
{
    bPollingActive = false;
    if (PollingTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PollingTickHandle);
        PollingTickHandle.Reset();
    }
    if (PollingHttpRequest.IsValid())
    {
        PollingHttpRequest->CancelRequest();
        PollingHttpRequest.Reset();
    }
    bPollingRequestInFlight = false;
}

bool SUnrealOpenCodePanel::OnLivePollingTick(float)
{
    if (!bPollingActive) return false;
    if (!bPollingRequestInFlight)
    {
        FirePollingRequest();
    }
    return true; // keep ticker alive
}

void SUnrealOpenCodePanel::FirePollingRequest()
{
    if (OpenCodeSessionId.IsEmpty()) return;
    bPollingRequestInFlight = true;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(FString::Printf(TEXT("http://localhost:%d/session/%s/message"),
        OpenCodePort, *OpenCodeSessionId));
    Req->SetVerb(TEXT("GET"));
    Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Req->SetTimeout(10.0f);

    TWeakPtr<SUnrealOpenCodePanel> FireWeakThis = SharedThis(this);
    Req->OnProcessRequestComplete().BindLambda(
        [FireWeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
        {
            TSharedPtr<SUnrealOpenCodePanel> Panel = FireWeakThis.Pin();
            if (!Panel.IsValid()) return;
            Panel->bPollingRequestInFlight = false;
            if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
            {
                Panel->ProcessPollingResponse(Resp->GetContentAsString());
            }
        });

    PollingHttpRequest = Req;
    Req->ProcessRequest();
}

void SUnrealOpenCodePanel::SnapshotExistingPartIds(const FString& Json)
{
    // Scan for all part IDs ("id":"prt_) and mark them as already known
    KnownPartIds.Empty();
    const FString IdKey = TEXT("\"id\":\"prt_");
    int32 SearchPos = 0;
    while (true)
    {
        int32 KeyPos = Json.Find(IdKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
        if (KeyPos == INDEX_NONE) break;
        int32 ValueStart = KeyPos + IdKey.Len() - 1; // include 'prt_'
        // Walk back one to include the 'p' of 'prt_'
        // Actually IdKey ends with 'prt_' so ValueStart is at character after '"'... let's recalc:
        // IdKey = "id":"prt_  — value starts at 'p'
        ValueStart = KeyPos + 6; // length of "id":" = 6, then value starts
        int32 ValueEnd = Json.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
        if (ValueEnd == INDEX_NONE) break;
        FString PartId = Json.Mid(ValueStart, ValueEnd - ValueStart);
        KnownPartIds.Add(PartId);
        ShownTextPartIds.Add(PartId);  // treat as already shown
        SearchPos = ValueEnd + 1;
    }
}

void SUnrealOpenCodePanel::ProcessPollingResponse(const FString& Json)
{
    if (!bPollingActive) return;

    bool bChanged = false;

    // We need to find each part object. Strategy: find each occurrence of "id":"prt_"
    // and look backward for the type, and look around for tool name / text content.
    const FString IdKey = TEXT("\"id\":\"prt_");
    int32 SearchPos = 0;

    while (true)
    {
        int32 KeyPos = Json.Find(IdKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
        if (KeyPos == INDEX_NONE) break;

        // Extract part ID
        int32 IdValueStart = KeyPos + 6; // skip "id":"
        int32 IdValueEnd = Json.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, IdValueStart);
        if (IdValueEnd == INDEX_NONE) break;
        FString PartId = Json.Mid(IdValueStart, IdValueEnd - IdValueStart);
        SearchPos = IdValueEnd + 1;

        // Skip parts we already know about from pre-flight snapshot
        if (KnownPartIds.Contains(PartId)) continue;

        // Search backward up to 2000 chars for "type":"
        int32 WindowStart = FMath::Max(0, KeyPos - 2000);
        FString Window = Json.Mid(WindowStart, KeyPos - WindowStart);

        // Find LAST occurrence of "type":" in window
        const FString TypeKey = TEXT("\"type\":\"");
        int32 TypePos = INDEX_NONE;
        int32 TSearch = 0;
        while (true)
        {
            int32 Found = Window.Find(TypeKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, TSearch);
            if (Found == INDEX_NONE) break;
            TypePos = Found;
            TSearch = Found + 1;
        }
        if (TypePos == INDEX_NONE) continue;

        int32 TypeValueStart = TypePos + TypeKey.Len();
        int32 TypeValueEnd = Window.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, TypeValueStart);
        if (TypeValueEnd == INDEX_NONE) continue;
        FString PartType = Window.Mid(TypeValueStart, TypeValueEnd - TypeValueStart);

        if (PartType == TEXT("text"))
        {
            // Skip if already shown
            if (ShownTextPartIds.Contains(PartId)) continue;
            ShownTextPartIds.Add(PartId);

            // Extract text value — look forward from type position in full JSON
            int32 TextSearchStart = WindowStart + TypePos;
            const FString TextKey = TEXT("\"text\":\"");
            int32 TextPos = Json.Find(TextKey, ESearchCase::CaseSensitive, ESearchDir::FromStart, TextSearchStart);
            if (TextPos == INDEX_NONE || TextPos > KeyPos + 200) continue;

            int32 TextValueStart = TextPos + TextKey.Len();
            // Read until unescaped closing quote
            FString Chunk;
            int32 Pos = TextValueStart;
            while (Pos < Json.Len())
            {
                TCHAR Ch = Json[Pos];
                if (Ch == TEXT('\\') && Pos + 1 < Json.Len())
                {
                    TCHAR Next = Json[Pos + 1];
                    if (Next == TEXT('n'))       { Chunk += TEXT('\n'); Pos += 2; continue; }
                    else if (Next == TEXT('r')) { Pos += 2; continue; }
                    else if (Next == TEXT('t')) { Chunk += TEXT('\t'); Pos += 2; continue; }
                    else                        { Chunk += Next;        Pos += 2; continue; }
                }
                if (Ch == TEXT('"')) break;
                Chunk += Ch;
                ++Pos;
            }
            if (!Chunk.IsEmpty())
            {
                if (!LiveText.IsEmpty()) LiveText += TEXT("\n");
                LiveText += Chunk;
                bChanged = true;
            }
        }
        else if (PartType == TEXT("tool"))
        {
            // Extract tool name from window
            const FString ToolKey = TEXT("\"tool\":\"");
            int32 ToolPos = Window.Find(ToolKey, ESearchCase::CaseSensitive);
            FString ToolName;
            if (ToolPos != INDEX_NONE)
            {
                int32 ToolValStart = ToolPos + ToolKey.Len();
                int32 ToolValEnd = Window.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ToolValStart);
                if (ToolValEnd != INDEX_NONE)
                    ToolName = Window.Mid(ToolValStart, ToolValEnd - ToolValStart);
            }

            // Determine completion status by looking forward from ID
            FString AfterWindow = Json.Mid(KeyPos, FMath::Min(500, Json.Len() - KeyPos));
            bool bCompleted = AfterWindow.Contains(TEXT("\"status\":\"completed\""))
                           || AfterWindow.Contains(TEXT("\"status\":\"error\""));

            FString DisplayName = FormatToolDisplayName(ToolName);
            FLiveToolEntry* Existing = LiveToolParts.Find(PartId);
            if (Existing == nullptr)
            {
                FLiveToolEntry Entry;
                Entry.DisplayName = DisplayName;
                Entry.bCompleted = bCompleted;
                LiveToolParts.Add(PartId, Entry);
                bChanged = true;
            }
            else if (!Existing->bCompleted && bCompleted)
            {
                Existing->bCompleted = true;
                bChanged = true;
            }
        }
    }

    if (bChanged)
    {
        UpdateLiveThinkingMessage();
    }
}

void SUnrealOpenCodePanel::UpdateLiveThinkingMessage()
{
    if (ThinkingMessageIndex < 0 || ThinkingMessageIndex >= Messages.Num()) return;

    FString Display;

    // Tool call lines (ordered by insertion — TMap doesn't preserve order, but that's fine)
    for (const TPair<FString, FLiveToolEntry>& Pair : LiveToolParts)
    {
        const FLiveToolEntry& Entry = Pair.Value;
        FString StatusStr = Entry.bCompleted ? TEXT("  \u2713") : TEXT("  ...");
        Display += Entry.DisplayName + StatusStr + TEXT("\n");
    }

    // Separator only if we have both tools and text
    if (!LiveToolParts.IsEmpty() && !LiveText.IsEmpty())
    {
        Display += TEXT("\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n");
    }

    if (!LiveText.IsEmpty())
    {
        Display += LiveText;
    }

    if (Display.IsEmpty())
    {
        Display = TEXT("...");
    }

    Messages[ThinkingMessageIndex]->Text = Display;
    Messages[ThinkingMessageIndex]->ParseMessageSegments();
    RefreshMessageAreaOnly();
}

void SUnrealOpenCodePanel::RefreshMessageAreaOnly()
{
    // Like RebuildMessageArea but skip the sidebar rebuild for efficiency
    if (ContentSwitcher.IsValid())
    {
        ContentSwitcher->SetActiveWidgetIndex(GetContentSwitcherIndex());
    }

    if (Messages.Num() > 0 && MessageScrollBox.IsValid())
    {
        MessageScrollBox->ClearChildren();
        for (const auto& Msg : Messages)
        {
            MessageScrollBox->AddSlot()
            .Padding(0.0f, 4.0f)
            [
                BuildMessageWidget(Msg)
            ];
        }
        ScrollToBottom();
    }
}

FString SUnrealOpenCodePanel::FormatToolDisplayName(const FString& RawToolName)
{
    if (RawToolName.IsEmpty()) return TEXT("[?] unknown");

    // UnrealOpenCode MCP tools
    if (RawToolName.StartsWith(TEXT("unrealopencode_")))
    {
        FString Short = RawToolName.Mid(15); // strip 'unrealopencode_'
        return TEXT("[UE] ") + Short;
    }

    // File tools
    if (RawToolName == TEXT("read")  || RawToolName == TEXT("write") ||
        RawToolName == TEXT("edit")  || RawToolName == TEXT("glob")  ||
        RawToolName == TEXT("grep"))
    {
        return TEXT("[File] ") + RawToolName;
    }

    // Shell
    if (RawToolName == TEXT("bash")) return TEXT("[Shell] bash");

    // Agent delegation
    if (RawToolName == TEXT("task")) return TEXT("[Agent] task");

    return TEXT("[?] ") + RawToolName;
}

// ---------------------------------------------------------------------------
// Markdown Rendering
// ---------------------------------------------------------------------------

TArray<FUEOCInlineSpan> SUnrealOpenCodePanel::ParseInlineSpans(const FString& Text)
{
    TArray<FUEOCInlineSpan> Result;
    if (Text.IsEmpty()) return Result;

    int32 Pos = 0;
    FString Accum;

    // Helper: flush accumulated plain text as a Plain span
    auto FlushPlain = [&]()
    {
        if (!Accum.IsEmpty())
        {
            FUEOCInlineSpan Span;
            Span.Text = Accum;
            Span.Type = EUEOCSpanType::Plain;
            Result.Add(Span);
            Accum.Empty();
        }
    };

    while (Pos < Text.Len())
    {
        TCHAR Ch = Text[Pos];

        // Inline code: backtick
        if (Ch == TEXT('`'))
        {
            int32 End = Text.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 1);
            if (End != INDEX_NONE && End > Pos)
            {
                FlushPlain();
                FUEOCInlineSpan Span;
                Span.Text = Text.Mid(Pos + 1, End - Pos - 1);
                Span.Type = EUEOCSpanType::Code;
                if (!Span.Text.IsEmpty()) Result.Add(Span);
                Pos = End + 1;
                continue;
            }
        }
        // Bold: **text**
        else if (Ch == TEXT('*') && Pos + 1 < Text.Len() && Text[Pos + 1] == TEXT('*'))
        {
            int32 End = Text.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 2);
            if (End != INDEX_NONE && End > Pos + 1)
            {
                FlushPlain();
                FUEOCInlineSpan Span;
                Span.Text = Text.Mid(Pos + 2, End - Pos - 2);
                Span.Type = EUEOCSpanType::Bold;
                if (!Span.Text.IsEmpty()) Result.Add(Span);
                Pos = End + 2;
                continue;
            }
        }
        // Bold: __text__
        else if (Ch == TEXT('_') && Pos + 1 < Text.Len() && Text[Pos + 1] == TEXT('_'))
        {
            int32 End = Text.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 2);
            if (End != INDEX_NONE && End > Pos + 1)
            {
                FlushPlain();
                FUEOCInlineSpan Span;
                Span.Text = Text.Mid(Pos + 2, End - Pos - 2);
                Span.Type = EUEOCSpanType::Bold;
                if (!Span.Text.IsEmpty()) Result.Add(Span);
                Pos = End + 2;
                continue;
            }
        }
        // Italic: *text* (only single *, not followed by another *)
        else if (Ch == TEXT('*') && (Pos + 1 >= Text.Len() || Text[Pos + 1] != TEXT('*')))
        {
            int32 End = Text.Find(TEXT("*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 1);
            // Ensure closing * is also not a double-star
            if (End != INDEX_NONE && End > Pos && (End + 1 >= Text.Len() || Text[End + 1] != TEXT('*')))
            {
                FlushPlain();
                FUEOCInlineSpan Span;
                Span.Text = Text.Mid(Pos + 1, End - Pos - 1);
                Span.Type = EUEOCSpanType::Italic;
                if (!Span.Text.IsEmpty()) Result.Add(Span);
                Pos = End + 1;
                continue;
            }
        }

        Accum += Ch;
        ++Pos;
    }

    FlushPlain();
    return Result;
}

TArray<FUEOCMarkdownLine> SUnrealOpenCodePanel::ParseMarkdownLines(const FString& Text)
{
    TArray<FString> RawLines;
    Text.ParseIntoArrayLines(RawLines, false);

    TArray<FUEOCMarkdownLine> Result;

    for (const FString& RawLine : RawLines)
    {
        FUEOCMarkdownLine Line;

        // Count leading spaces/tabs for indent level
        int32 LeadingSpaces = 0;
        while (LeadingSpaces < RawLine.Len() &&
               (RawLine[LeadingSpaces] == TEXT(' ') || RawLine[LeadingSpaces] == TEXT('\t')))
        {
            ++LeadingSpaces;
        }
        Line.IndentLevel = LeadingSpaces / 2;
        FString Trimmed = RawLine.Mid(LeadingSpaces);

        // Empty line
        if (Trimmed.IsEmpty())
        {
            Line.Type = EUEOCLineType::Empty;
            Result.Add(Line);
            continue;
        }

        // Horizontal rule: 3+ of -, =, \u2500 (\u2500), \u2550
        if (Trimmed.Len() >= 3)
        {
            TCHAR First = Trimmed[0];
            if (First == TEXT('-') || First == TEXT('=') ||
                (uint32)First == 0x2500 || (uint32)First == 0x2550 || (uint32)First == 0x2501)
            {
                bool bAllHRule = true;
                for (int32 i = 1; i < FMath::Min(Trimmed.Len(), 5); ++i)
                {
                    TCHAR C = Trimmed[i];
                    if (C != First && C != TEXT('-') && C != TEXT('=')
                        && (uint32)C != 0x2500 && (uint32)C != 0x2550 && (uint32)C != 0x2501)
                    {
                        bAllHRule = false;
                        break;
                    }
                }
                if (bAllHRule)
                {
                    Line.Type = EUEOCLineType::HRule;
                    Result.Add(Line);
                    continue;
                }
            }
        }

        // Headers
        if (Trimmed.StartsWith(TEXT("### ")))
        {
            Line.Type = EUEOCLineType::Header3;
            Line.Spans = ParseInlineSpans(Trimmed.Mid(4));
        }
        else if (Trimmed.StartsWith(TEXT("## ")))
        {
            Line.Type = EUEOCLineType::Header2;
            Line.Spans = ParseInlineSpans(Trimmed.Mid(3));
        }
        else if (Trimmed.StartsWith(TEXT("# ")))
        {
            Line.Type = EUEOCLineType::Header1;
            Line.Spans = ParseInlineSpans(Trimmed.Mid(2));
        }
        else
        {
            // Numbered list: digits followed by ". "
            int32 NumLen = 0;
            while (NumLen < Trimmed.Len() && FChar::IsDigit(Trimmed[NumLen])) ++NumLen;

            if (NumLen > 0 && NumLen + 1 < Trimmed.Len()
                && Trimmed[NumLen] == TEXT('.') && Trimmed[NumLen + 1] == TEXT(' '))
            {
                Line.Type = EUEOCLineType::NumberedList;
                Line.ListPrefix = Trimmed.Left(NumLen + 1); // e.g. "1."
                Line.Spans = ParseInlineSpans(Trimmed.Mid(NumLen + 2));
            }
            // Bullet list: -, *, +, \u2022, \u2013, \u2014 followed by space
            else if (Trimmed.Len() >= 2 && Trimmed[1] == TEXT(' '))
            {
                TCHAR C0 = Trimmed[0];
                if (C0 == TEXT('-') || C0 == TEXT('*') || C0 == TEXT('+')
                    || (uint32)C0 == 0x2022 || (uint32)C0 == 0x2013 || (uint32)C0 == 0x2014)
                {
                    Line.Type = EUEOCLineType::BulletList;
                    Line.ListPrefix = TEXT("\u2022");
                    Line.Spans = ParseInlineSpans(Trimmed.Mid(2));
                }
            }
            // Normal text
            if (Line.Type == EUEOCLineType::Normal)
            {
                Line.Spans = ParseInlineSpans(Trimmed);
            }
        }

        Result.Add(Line);
    }

    return Result;
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildInlineSpansWidget(
    const TArray<FUEOCInlineSpan>& Spans, const FLinearColor& BaseColor, bool bBold)
{
    const FLinearColor CodeColor  (0.00f, 0.88f, 0.88f, 1.0f); // cyan
    const FLinearColor BoldColor  (0.95f, 0.62f, 0.05f, 1.0f); // orange
    const FLinearColor ItalicColor(0.72f, 0.72f, 0.92f, 1.0f); // light blue-gray

    if (Spans.Num() == 0)
    {
        return SNew(STextBlock).AutoWrapText(true);
    }

    // Optimisation: single plain span — direct STextBlock with AutoWrapText
    if (Spans.Num() == 1 && Spans[0].Type == EUEOCSpanType::Plain)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Spans[0].Text))
            .ColorAndOpacity(FSlateColor(BaseColor))
            .Font(bBold ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont"))
            .AutoWrapText(true);
    }

    // Mixed spans: horizontal box
    // Last slot gets FillWidth so trailing plain text wraps properly.
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

    for (int32 i = 0; i < Spans.Num(); ++i)
    {
        const FUEOCInlineSpan& Span = Spans[i];
        const bool bIsLast = (i == Spans.Num() - 1);

        FLinearColor SpanColor;
        FSlateFontInfo SpanFont;

        switch (Span.Type)
        {
        case EUEOCSpanType::Code:
            SpanColor = CodeColor;
            SpanFont  = FAppStyle::GetFontStyle("MonoFont");
            break;
        case EUEOCSpanType::Bold:
            SpanColor = BoldColor;
            SpanFont  = FAppStyle::GetFontStyle("BoldFont");
            break;
        case EUEOCSpanType::Italic:
            SpanColor = ItalicColor;
            SpanFont  = FAppStyle::GetFontStyle("NormalFont");
            break;
        default: // Plain
            SpanColor = BaseColor;
            SpanFont  = bBold ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont");
            break;
        }

        if (bIsLast)
        {
            Row->AddSlot()
            .FillWidth(1.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Span.Text))
                .ColorAndOpacity(FSlateColor(SpanColor))
                .Font(SpanFont)
                .AutoWrapText(true)
            ];
        }
        else
        {
            Row->AddSlot()
            .AutoWidth()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Span.Text))
                .ColorAndOpacity(FSlateColor(SpanColor))
                .Font(SpanFont)
            ];
        }
    }

    return Row;
}

TSharedRef<SWidget> SUnrealOpenCodePanel::BuildMarkdownWidget(const TArray<FUEOCMarkdownLine>& Lines)
{
    const FLinearColor NormalColor  (0.88f, 0.88f, 0.88f, 1.0f); // light gray
    const FLinearColor HeaderColor  (0.95f, 0.62f, 0.05f, 1.0f); // orange
    const FLinearColor ListNumColor (0.95f, 0.62f, 0.05f, 1.0f); // orange
    const FLinearColor BulletColor  (0.50f, 0.50f, 0.50f, 1.0f); // gray
    const FLinearColor HRuleColor   (0.25f, 0.25f, 0.25f, 1.0f); // dark gray

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    for (const FUEOCMarkdownLine& Line : Lines)
    {
        const float LeftPad = Line.IndentLevel * 16.0f;

        switch (Line.Type)
        {
        case EUEOCLineType::Empty:
            Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [ SNullWidget::NullWidget ];
            break;

        case EUEOCLineType::HRule:
            Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .BorderBackgroundColor(HRuleColor)
                .Padding(0.0f, 1.0f)
            ];
            break;

        case EUEOCLineType::Header1:
        case EUEOCLineType::Header2:
        case EUEOCLineType::Header3:
            Box->AddSlot().AutoHeight().Padding(LeftPad, 5.0f, 0.0f, 2.0f)
            [
                BuildInlineSpansWidget(Line.Spans, HeaderColor, true)
            ];
            break;

        case EUEOCLineType::NumberedList:
            Box->AddSlot().AutoHeight().Padding(LeftPad, 1.0f, 0.0f, 1.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 5.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Line.ListPrefix))
                    .ColorAndOpacity(FSlateColor(ListNumColor))
                    .Font(FAppStyle::GetFontStyle("NormalFont"))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    BuildInlineSpansWidget(Line.Spans, NormalColor, false)
                ]
            ];
            break;

        case EUEOCLineType::BulletList:
            Box->AddSlot().AutoHeight().Padding(LeftPad, 1.0f, 0.0f, 1.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 5.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u2022")))
                    .ColorAndOpacity(FSlateColor(BulletColor))
                    .Font(FAppStyle::GetFontStyle("NormalFont"))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    BuildInlineSpansWidget(Line.Spans, NormalColor, false)
                ]
            ];
            break;

        case EUEOCLineType::Normal:
        default:
            Box->AddSlot().AutoHeight().Padding(LeftPad, 1.0f, 0.0f, 1.0f)
            [
                BuildInlineSpansWidget(Line.Spans, NormalColor, false)
            ];
            break;
        }
    }

    return Box;
}
