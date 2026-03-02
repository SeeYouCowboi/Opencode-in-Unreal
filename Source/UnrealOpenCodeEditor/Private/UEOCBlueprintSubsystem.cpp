#include "UEOCBlueprintSubsystem.h"

#include "UEOCTCPServerSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "UnrealOpenCodeProtocol.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/FieldIterator.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

void UUEOCBlueprintSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UUEOCTCPServerSubsystem>();

	if (GEditor == nullptr)
	{
		return;
	}

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		RequestDelegateHandle = TCPSubsystem->OnJsonRequestReceived.AddUObject(this, &UUEOCBlueprintSubsystem::HandleRequest);
	}
}

void UUEOCBlueprintSubsystem::Deinitialize()
{
	if (GEditor != nullptr && RequestDelegateHandle.IsValid())
	{
		if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
		{
			TCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
		}
	}

	RequestDelegateHandle.Reset();
	Super::Deinitialize();
}

void UUEOCBlueprintSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> RequestJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);
	if (!FJsonSerializer::Deserialize(Reader, RequestJson) || !RequestJson.IsValid())
	{
		return;
	}

	FString RequestId;
	if (!RequestJson->TryGetStringField(TEXT("id"), RequestId))
	{
		return;
	}

	FString Type;
	if (!RequestJson->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}

    TSharedPtr<FJsonObject> Params;
    if (RequestJson->HasTypedField<EJson::Object>(TEXT("params")))
    {
        Params = RequestJson->GetObjectField(TEXT("params"));
    }
    if (!Params.IsValid())
    {
        Params = MakeShareable(new FJsonObject());
    }

	if (Type == UEOCToolTypes::GetBlueprintList)
	{
		HandleGetBlueprintList(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::GetBlueprintDetails)
	{
		HandleGetBlueprintDetails(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::SearchBlueprints)
	{
		HandleSearchBlueprints(RequestId, Params);
	}
}

void UUEOCBlueprintSubsystem::HandleGetBlueprintList(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
    (void)Params;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> BlueprintAssets;
	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	TArray<TSharedPtr<FJsonValue>> BlueprintArray;
	BlueprintArray.Reserve(BlueprintAssets.Num());

	for (const FAssetData& Asset : BlueprintAssets)
	{
		FString ParentClassPath;
		Asset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath);

		FString BlueprintType;
		Asset.GetTagValue(FBlueprintTags::BlueprintType, BlueprintType);

		TSharedPtr<FJsonObject> BPObj = MakeShareable(new FJsonObject());
		BPObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		BPObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		BPObj->SetStringField(TEXT("parentClass"), ParentClassPath);
		BPObj->SetStringField(TEXT("blueprintType"), BlueprintType);
		BlueprintArray.Add(MakeShareable(new FJsonValueObject(BPObj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetArrayField(TEXT("blueprints"), BlueprintArray);
	Data->SetNumberField(TEXT("count"), BlueprintAssets.Num());

	SendResponse(RequestId, UEOCToolTypes::GetBlueprintList, Data);
}

void UUEOCBlueprintSubsystem::HandleGetBlueprintDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString BlueprintPath;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		SendErrorResponse(RequestId, UEOCToolTypes::GetBlueprintDetails, -32602, TEXT("Missing required param: blueprintPath"));
		return;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(BlueprintPath));
	if (!AssetData.IsValid())
	{
		SendErrorResponse(RequestId, UEOCToolTypes::GetBlueprintDetails, 404, TEXT("Blueprint not found"));
		return;
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("path"), AssetData.GetObjectPathString());

	FString ParentClassPath;
	AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath);
	Data->SetStringField(TEXT("parentClass"), ParentClassPath);

	TArray<TSharedPtr<FJsonValue>> Variables;
	TArray<TSharedPtr<FJsonValue>> Functions;

	if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
	{
		if (UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;

				TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
				VarObj->SetStringField(TEXT("name"), Property->GetName());
				VarObj->SetStringField(TEXT("type"), Property->GetCPPType());
				VarObj->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
				Variables.Add(MakeShareable(new FJsonValueObject(VarObj)));
			}

			for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const UFunction* Function = *It;
				const bool bIsCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent);

				TSharedPtr<FJsonObject> FunctionObj = MakeShareable(new FJsonObject());
				FunctionObj->SetStringField(TEXT("name"), Function->GetName());
				FunctionObj->SetBoolField(TEXT("isCallable"), bIsCallable);
				Functions.Add(MakeShareable(new FJsonValueObject(FunctionObj)));
			}
		}
	}

	Data->SetArrayField(TEXT("variables"), Variables);
	Data->SetArrayField(TEXT("functions"), Functions);

	SendResponse(RequestId, UEOCToolTypes::GetBlueprintDetails, Data);
}

void UUEOCBlueprintSubsystem::HandleSearchBlueprints(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString Query;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("query"), Query);
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> BlueprintAssets;
	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	TArray<TSharedPtr<FJsonValue>> Matches;
	Matches.Reserve(20);

	for (const FAssetData& Asset : BlueprintAssets)
	{
		if (Matches.Num() >= 20)
		{
			break;
		}

		const FString Name = Asset.AssetName.ToString();
		const FString Path = Asset.GetObjectPathString();
		if (!Query.IsEmpty() && !Name.Contains(Query, ESearchCase::IgnoreCase) && !Path.Contains(Query, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FString ParentClassPath;
		Asset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath);

		TSharedPtr<FJsonObject> BPObj = MakeShareable(new FJsonObject());
		BPObj->SetStringField(TEXT("name"), Name);
		BPObj->SetStringField(TEXT("path"), Path);
		BPObj->SetStringField(TEXT("parentClass"), ParentClassPath);
		Matches.Add(MakeShareable(new FJsonValueObject(BPObj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
	Data->SetArrayField(TEXT("blueprints"), Matches);
	Data->SetNumberField(TEXT("count"), Matches.Num());

	SendResponse(RequestId, UEOCToolTypes::SearchBlueprints, Data);
}

void UUEOCBlueprintSubsystem::SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson)
{
	if (GEditor == nullptr)
	{
		return;
	}

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem == nullptr)
	{
		return;
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("data"), DataJson.IsValid() ? DataJson : MakeShareable(new FJsonObject()));
	Response->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString ResponseStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	TCPSubsystem->SendJsonResponse(ResponseStr);
}

void UUEOCBlueprintSubsystem::SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message)
{
	if (GEditor == nullptr)
	{
		return;
	}

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem == nullptr)
	{
		return;
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), false);

	TSharedPtr<FJsonObject> ErrorJson = MakeShareable(new FJsonObject());
	ErrorJson->SetNumberField(TEXT("code"), Code);
	ErrorJson->SetStringField(TEXT("message"), Message);
	Response->SetObjectField(TEXT("error"), ErrorJson);

	Response->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString ResponseStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	TCPSubsystem->SendJsonResponse(ResponseStr);
}
