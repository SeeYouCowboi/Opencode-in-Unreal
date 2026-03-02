// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEOCSceneAssetSubsystem.h"
#include "UEOCTCPServerSubsystem.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Selection.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUEOCSceneAssetSubsystem);

void UUEOCSceneAssetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UUEOCTCPServerSubsystem>();

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		RequestDelegateHandle = TCPSubsystem->OnJsonRequestReceived.AddUObject(
			this, &UUEOCSceneAssetSubsystem::HandleRequest);

		UE_LOG(LogUEOCSceneAssetSubsystem, Log,
			TEXT("Scene/Asset subsystem initialized and bound to TCP server"));
	}
	else
	{
		UE_LOG(LogUEOCSceneAssetSubsystem, Warning,
			TEXT("TCP server subsystem not available — scene/asset handlers will not work"));
	}
}

void UUEOCSceneAssetSubsystem::Deinitialize()
{
	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		TCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
	}

	UE_LOG(LogUEOCSceneAssetSubsystem, Log, TEXT("Scene/Asset subsystem deinitialized"));

	Super::Deinitialize();
}

void UUEOCSceneAssetSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	FString RequestId = Root->GetStringField(TEXT("id"));
	FString Method = Root->GetStringField(TEXT("method"));
	TSharedPtr<FJsonObject> Params = Root->GetObjectField(TEXT("params"));

	if (Method == TEXT("get_scene_hierarchy"))
	{
		HandleGetSceneHierarchy(RequestId, Params);
	}
	else if (Method == TEXT("get_actor_details"))
	{
		HandleGetActorDetails(RequestId, Params);
	}
	else if (Method == TEXT("get_selected_actors"))
	{
		HandleGetSelectedActors(RequestId, Params);
	}
	else if (Method == TEXT("search_assets"))
	{
		HandleSearchAssets(RequestId, Params);
	}
	else if (Method == TEXT("get_asset_details"))
	{
		HandleGetAssetDetails(RequestId, Params);
	}
	else if (Method == TEXT("get_asset_references"))
	{
		HandleGetAssetReferences(RequestId, Params);
	}
}

// ---- Scene handlers ----

void UUEOCSceneAssetSubsystem::HandleGetSceneHierarchy(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, TEXT("get_scene_hierarchy"), -1, TEXT("No editor world available"));
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Actors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject);
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetBoolField(TEXT("isRootActor"), Actor->GetAttachParentActor() == nullptr);

		if (Actor->GetAttachParentActor())
		{
			ActorObj->SetStringField(TEXT("parentName"), Actor->GetAttachParentActor()->GetName());
		}

		// Transform
		FTransform T = Actor->GetActorTransform();
		TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject);
		TObj->SetStringField(TEXT("location"), T.GetLocation().ToString());
		TObj->SetStringField(TEXT("rotation"), T.GetRotation().Rotator().ToString());
		TObj->SetStringField(TEXT("scale"), T.GetScale3D().ToString());
		ActorObj->SetObjectField(TEXT("transform"), TObj);

		Actors.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetArrayField(TEXT("actors"), Actors);
	SendResponse(RequestId, TEXT("get_scene_hierarchy"), Data);
}

void UUEOCSceneAssetSubsystem::HandleGetActorDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString ActorName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("name"), ActorName))
	{
		SendErrorResponse(RequestId, TEXT("get_actor_details"), -1, TEXT("Missing required param: name"));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		SendErrorResponse(RequestId, TEXT("get_actor_details"), -1, TEXT("No editor world available"));
		return;
	}

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		SendErrorResponse(RequestId, TEXT("get_actor_details"), -1,
			FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		return;
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("name"), FoundActor->GetName());
	Data->SetStringField(TEXT("label"), FoundActor->GetActorLabel());
	Data->SetStringField(TEXT("class"), FoundActor->GetClass()->GetName());
	Data->SetStringField(TEXT("pathName"), FoundActor->GetPathName());

	// Transform
	FTransform T = FoundActor->GetActorTransform();
	TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject);
	TObj->SetStringField(TEXT("location"), T.GetLocation().ToString());
	TObj->SetStringField(TEXT("rotation"), T.GetRotation().Rotator().ToString());
	TObj->SetStringField(TEXT("scale"), T.GetScale3D().ToString());
	Data->SetObjectField(TEXT("transform"), TObj);

	// Components
	TArray<TSharedPtr<FJsonValue>> ComponentArray;
	TSet<UActorComponent*> Components = FoundActor->GetComponents();
	for (UActorComponent* Comp : Components)
	{
		if (!Comp)
		{
			continue;
		}
		TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		ComponentArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
	}
	Data->SetArrayField(TEXT("components"), ComponentArray);

	// Tags
	TArray<TSharedPtr<FJsonValue>> TagArray;
	for (const FName& Tag : FoundActor->Tags)
	{
		TagArray.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
	}
	Data->SetArrayField(TEXT("tags"), TagArray);

	SendResponse(RequestId, TEXT("get_actor_details"), Data);
}

void UUEOCSceneAssetSubsystem::HandleGetSelectedActors(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	TArray<TSharedPtr<FJsonValue>> Selected;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), Actor->GetName());
		Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());

		FTransform T = Actor->GetActorTransform();
		TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject);
		TObj->SetStringField(TEXT("location"), T.GetLocation().ToString());
		TObj->SetStringField(TEXT("rotation"), T.GetRotation().Rotator().ToString());
		TObj->SetStringField(TEXT("scale"), T.GetScale3D().ToString());
		Obj->SetObjectField(TEXT("transform"), TObj);

		Selected.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetArrayField(TEXT("selectedActors"), Selected);
	SendResponse(RequestId, TEXT("get_selected_actors"), Data);
}

// ---- Asset handlers ----

void UUEOCSceneAssetSubsystem::HandleSearchAssets(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FString Query;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("query"), Query);
	}

	FString AssetType;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("assetType"), AssetType);
	}

	int32 Limit = 100;
	int32 Offset = 0;
	if (Params.IsValid())
	{
		Params->TryGetNumberField(TEXT("limit"), Limit);
		Params->TryGetNumberField(TEXT("offset"), Offset);
	}
	Limit = FMath::Clamp(Limit, 1, 500);
	Offset = FMath::Max(0, Offset);

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (!AssetType.IsEmpty())
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), *AssetType));
	}

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAssets(Filter, AllAssets);

	// Filter by query name if provided
	TArray<FAssetData> FilteredAssets;
	if (Query.IsEmpty())
	{
		FilteredAssets = AllAssets;
	}
	else
	{
		for (const FAssetData& Asset : AllAssets)
		{
			if (Asset.AssetName.ToString().Contains(Query))
			{
				FilteredAssets.Add(Asset);
			}
		}
	}

	int32 TotalCount = FilteredAssets.Num();

	// Apply pagination
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 End = FMath::Min(Offset + Limit, TotalCount);
	for (int32 i = Offset; i < End; ++i)
	{
		const FAssetData& Asset = FilteredAssets[i];
		TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject);
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetObj->SetStringField(TEXT("packagePath"), Asset.PackagePath.ToString());
		Results.Add(MakeShareable(new FJsonValueObject(AssetObj)));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetArrayField(TEXT("assets"), Results);
	Data->SetNumberField(TEXT("totalCount"), TotalCount);
	Data->SetNumberField(TEXT("offset"), Offset);
	Data->SetNumberField(TEXT("limit"), Limit);
	SendResponse(RequestId, TEXT("search_assets"), Data);
}

void UUEOCSceneAssetSubsystem::HandleGetAssetDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString AssetPath;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		SendErrorResponse(RequestId, TEXT("get_asset_details"), -1, TEXT("Missing required param: path"));
		return;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	if (!AssetData.IsValid())
	{
		SendErrorResponse(RequestId, TEXT("get_asset_details"), -1,
			FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		return;
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
	Data->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
	Data->SetStringField(TEXT("packageName"), AssetData.PackageName.ToString());
	Data->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
	Data->SetBoolField(TEXT("isLoaded"), AssetData.IsAssetLoaded());

	// Metadata tags
	TSharedPtr<FJsonObject> TagsObj = MakeShareable(new FJsonObject);
	AssetData.TagsAndValues.ForEach([&TagsObj](const TPair<FName, FAssetTagValueRef>& Pair)
	{
		TagsObj->SetStringField(Pair.Key.ToString(), Pair.Value.AsString());
	});
	Data->SetObjectField(TEXT("tags"), TagsObj);

	SendResponse(RequestId, TEXT("get_asset_details"), Data);
}

void UUEOCSceneAssetSubsystem::HandleGetAssetReferences(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString AssetPath;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		SendErrorResponse(RequestId, TEXT("get_asset_references"), -1, TEXT("Missing required param: path"));
		return;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Verify asset exists
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	if (!AssetData.IsValid())
	{
		SendErrorResponse(RequestId, TEXT("get_asset_references"), -1,
			FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		return;
	}

	FName PackageName = AssetData.PackageName;

	// Referencers (assets that reference this one)
	TArray<FName> ReferencerNames;
	AssetRegistry.GetReferencers(PackageName, ReferencerNames);
	TArray<TSharedPtr<FJsonValue>> ReferencersArray;
	for (const FName& RefName : ReferencerNames)
	{
		ReferencersArray.Add(MakeShareable(new FJsonValueString(RefName.ToString())));
	}

	// Dependencies (assets this one references)
	TArray<FName> DependencyNames;
	AssetRegistry.GetDependencies(PackageName, DependencyNames);
	TArray<TSharedPtr<FJsonValue>> DependenciesArray;
	for (const FName& DepName : DependencyNames)
	{
		DependenciesArray.Add(MakeShareable(new FJsonValueString(DepName.ToString())));
	}

	TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject);
	Data->SetStringField(TEXT("assetPath"), AssetPath);
	Data->SetStringField(TEXT("packageName"), PackageName.ToString());
	Data->SetArrayField(TEXT("referencers"), ReferencersArray);
	Data->SetArrayField(TEXT("dependencies"), DependenciesArray);
	SendResponse(RequestId, TEXT("get_asset_references"), Data);
}

// ---- Response helpers ----

void UUEOCSceneAssetSubsystem::SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("data"), DataJson);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		TCPSubsystem->SendJsonResponse(OutputString);
	}
}

void UUEOCSceneAssetSubsystem::SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShareable(new FJsonObject);
	ErrorObj->SetNumberField(TEXT("code"), Code);
	ErrorObj->SetStringField(TEXT("message"), Message);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("id"), RequestId);
	Response->SetStringField(TEXT("type"), Type);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetObjectField(TEXT("error"), ErrorObj);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>();
	if (TCPSubsystem)
	{
		TCPSubsystem->SendJsonResponse(OutputString);
	}

	UE_LOG(LogUEOCSceneAssetSubsystem, Warning, TEXT("Error [%s] %d: %s"), *Type, Code, *Message);
}
