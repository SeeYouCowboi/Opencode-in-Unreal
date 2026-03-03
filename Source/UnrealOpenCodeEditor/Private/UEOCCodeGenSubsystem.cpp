#include "UEOCCodeGenSubsystem.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Layout/Visibility.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString UEOCEscapeJson(const FString& InValue)
	{
		FString Escaped = InValue;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Escaped;
	}

	void ComputeDiffSummary(const FString& ExistingContent, const FString& NewContent, int32& OutAddedLines, int32& OutRemovedLines)
	{
		TArray<FString> ExistingLines;
		TArray<FString> NewLines;
		ExistingContent.ParseIntoArrayLines(ExistingLines);
		NewContent.ParseIntoArrayLines(NewLines);

		const int32 ExistingCount = ExistingLines.Num();
		const int32 NewCount = NewLines.Num();

		TArray<int32> Prev;
		TArray<int32> Curr;
		Prev.Init(0, NewCount + 1);
		Curr.Init(0, NewCount + 1);

		for (int32 ExistingIndex = 1; ExistingIndex <= ExistingCount; ++ExistingIndex)
		{
			for (int32 NewIndex = 1; NewIndex <= NewCount; ++NewIndex)
			{
				if (ExistingLines[ExistingIndex - 1] == NewLines[NewIndex - 1])
				{
					Curr[NewIndex] = Prev[NewIndex - 1] + 1;
				}
				else
				{
					Curr[NewIndex] = FMath::Max(Curr[NewIndex - 1], Prev[NewIndex]);
				}
			}

			Swap(Prev, Curr);
			for (int32 ResetIndex = 0; ResetIndex <= NewCount; ++ResetIndex)
			{
				Curr[ResetIndex] = 0;
			}
		}

		const int32 CommonLines = Prev[NewCount];
		OutAddedLines = NewCount - CommonLines;
		OutRemovedLines = ExistingCount - CommonLines;
	}

	FString BuildPreviewText(const FString& AbsolutePath, const FString& Description, const FString& Content, bool bFileExists)
	{
		FString PreviewText = FString::Printf(
			TEXT("File Path:\n%s\n\nDescription:\n%s\n\n"),
			*AbsolutePath,
			Description.IsEmpty() ? TEXT("(none)") : *Description);

		if (bFileExists)
		{
			FString ExistingContent;
			FFileHelper::LoadFileToString(ExistingContent, *AbsolutePath);

			int32 AddedLines = 0;
			int32 RemovedLines = 0;
			ComputeDiffSummary(ExistingContent, Content, AddedLines, RemovedLines);

			PreviewText += FString::Printf(
				TEXT("Existing file detected.\nDiff summary: +%d / -%d lines\n\n"),
				AddedLines,
				RemovedLines);
		}

		PreviewText += TEXT("Content Preview:\n");
		PreviewText += Content;
		return PreviewText;
	}
}

void UUEOCCodeGenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UUEOCTCPServerSubsystem>();

	if (GEditor == nullptr)
	{
		return;
	}

	if (UUEOCTCPServerSubsystem* LocalTCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem = LocalTCPSubsystem;
		RequestDelegateHandle = LocalTCPSubsystem->OnJsonRequestReceived.AddUObject(this, &UUEOCCodeGenSubsystem::HandleRequest);
	}
}

void UUEOCCodeGenSubsystem::Deinitialize()
{
	if (GEditor != nullptr && RequestDelegateHandle.IsValid())
	{
		if (UUEOCTCPServerSubsystem* LocalTCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
		{
			LocalTCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
		}
	}

	RequestDelegateHandle.Reset();
	TCPSubsystem.Reset();

	Super::Deinitialize();
}

void UUEOCCodeGenSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	FString Type;
	FString RequestId;
	if (!Root->TryGetStringField(TEXT("type"), Type) || !Root->TryGetStringField(TEXT("id"), RequestId))
	{
		return;
	}

	if (Type != TEXT("generate_code"))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (!Root->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !Params->IsValid())
	{
		SendResponse(RequestId, false, TEXT("{}"), TEXT("Missing params object"));
		return;
	}

	FString FilePath;
	FString Content;
	FString Description;
	if (!(*Params)->TryGetStringField(TEXT("filePath"), FilePath)
		|| !(*Params)->TryGetStringField(TEXT("content"), Content)
		|| !(*Params)->TryGetStringField(TEXT("description"), Description))
	{
		SendResponse(RequestId, false, TEXT("{}"), TEXT("Missing required params: filePath, content, description"));
		return;
	}

	HandleGenerateCode(RequestId, FilePath, Content, Description);
}

void UUEOCCodeGenSubsystem::HandleGenerateCode(const FString& RequestId, const FString& FilePath, const FString& Content, const FString& Description)
{
	FString NormalizedProjectSourceDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));
	FPaths::NormalizeDirectoryName(NormalizedProjectSourceDir);

	FString CandidatePath = FilePath;
	if (FPaths::IsRelative(CandidatePath))
	{
		if (!CandidatePath.StartsWith(TEXT("Source/"), ESearchCase::IgnoreCase)
			&& !CandidatePath.StartsWith(TEXT("Source\\"), ESearchCase::IgnoreCase))
		{
			SendResponse(RequestId, false, TEXT("{}"), TEXT("filePath must be under Source/"));
			return;
		}

		CandidatePath = FPaths::Combine(FPaths::ProjectDir(), CandidatePath);
	}

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(CandidatePath);
	FPaths::NormalizeFilename(AbsolutePath);
	FPaths::CollapseRelativeDirectories(AbsolutePath);

	const bool bUnderSource = AbsolutePath.StartsWith(NormalizedProjectSourceDir + TEXT("/"), ESearchCase::IgnoreCase)
		|| AbsolutePath.Equals(NormalizedProjectSourceDir, ESearchCase::IgnoreCase);
	if (!bUnderSource)
	{
		SendResponse(RequestId, false, TEXT("{}"), TEXT("filePath is outside project Source/ directory"));
		return;
	}

	const bool bFileExists = IFileManager::Get().FileExists(*AbsolutePath);
	if (!ShowConfirmationDialog(AbsolutePath, Content, Description, bFileExists))
	{
		SendResponse(RequestId, false, TEXT("{}"), TEXT("Code generation cancelled by user"));
		return;
	}

	if (!WriteFile(AbsolutePath, Content, bFileExists))
	{
		SendResponse(RequestId, false, TEXT("{}"), TEXT("Failed to write file"));
		return;
	}

	const FString Action = bFileExists ? TEXT("overwrite") : TEXT("create");
	const FString DataJson = FString::Printf(
		TEXT("{\"filePath\":\"%s\",\"action\":\"%s\"}"),
		*UEOCEscapeJson(AbsolutePath),
		*Action);

	SendResponse(RequestId, true, DataJson);
}

bool UUEOCCodeGenSubsystem::ShowConfirmationDialog(const FString& FilePath, const FString& Content, const FString& Description, bool bFileExists)
{
	bool bConfirmed = false;
	const FString PreviewText = BuildPreviewText(FilePath, Description, Content, bFileExists);

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(TEXT("Code Generation Request")))
		.ClientSize(FVector2D(980.0f, 680.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.IsTopmostWindow(true);

	Dialog->SetContent(
		SNew(SBorder)
		.Padding(10.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Code Generation Request")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("File: %s"), *FilePath)))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Description: %s"), Description.IsEmpty() ? TEXT("(none)") : *Description)))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBox)
				.HeightOverride(200.0f)
				[
					SNew(SBorder)
					.Padding(6.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(SMultiLineEditableText)
							.Text(FText::FromString(PreviewText))
							.IsReadOnly(true)
							.Font(FAppStyle::Get().GetFontStyle(TEXT("Mono")))
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Lambda([Dialog]()
					{
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Create File")))
					.OnClicked_Lambda([Dialog, &bConfirmed]()
					{
						bConfirmed = true;
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, nullptr, false);
	return bConfirmed;
}

bool UUEOCCodeGenSubsystem::WriteFile(const FString& AbsolutePath, const FString& Content, bool bCreateBackup)
{
	if (bCreateBackup && IFileManager::Get().FileExists(*AbsolutePath))
	{
		const FString BackupPath = AbsolutePath + TEXT(".bak");
		if (IFileManager::Get().Copy(*BackupPath, *AbsolutePath, true, true) != COPY_OK)
		{
			return false;
		}
	}

	const FString Directory = FPaths::GetPath(AbsolutePath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}

	return FFileHelper::SaveStringToFile(Content, *AbsolutePath);
}

void UUEOCCodeGenSubsystem::SendResponse(const FString& RequestId, bool bSuccess, const FString& DataJson, const FString& ErrorMessage)
{
	UUEOCTCPServerSubsystem* LocalTCPSubsystem = TCPSubsystem.Get();
	if (LocalTCPSubsystem == nullptr && GEditor != nullptr)
	{
		LocalTCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
		TCPSubsystem = LocalTCPSubsystem;
	}

	if (LocalTCPSubsystem == nullptr)
	{
		return;
	}

	const int64 Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
	FString Json;
	if (bSuccess)
	{
		Json = FString::Printf(
			TEXT("{\"id\":\"%s\",\"type\":\"generate_code\",\"success\":true,\"data\":%s,\"timestamp\":%lld}"),
			*UEOCEscapeJson(RequestId),
			*DataJson,
			Timestamp);
	}
	else
	{
		Json = FString::Printf(
			TEXT("{\"id\":\"%s\",\"type\":\"generate_code\",\"success\":false,\"error\":{\"message\":\"%s\"},\"timestamp\":%lld}"),
			*UEOCEscapeJson(RequestId),
			*UEOCEscapeJson(ErrorMessage),
			Timestamp);
	}

	LocalTCPSubsystem->SendJsonResponse(Json);
}
