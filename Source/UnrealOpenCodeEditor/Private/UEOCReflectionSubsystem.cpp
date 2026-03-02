#include "UEOCReflectionSubsystem.h"

#include "UEOCTCPServerSubsystem.h"
#include "Editor.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "UnrealOpenCodeProtocol.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	static bool IsDefaultEngineModule(const FString& ModuleName)
	{
		return ModuleName.StartsWith(TEXT("/Script/Engine"))
			|| ModuleName.StartsWith(TEXT("/Script/CoreUObject"))
			|| ModuleName.StartsWith(TEXT("/Script/UnrealEd"))
			|| ModuleName.StartsWith(TEXT("/Script/Slate"))
			|| ModuleName.StartsWith(TEXT("/Script/SlateCore"))
			|| ModuleName.StartsWith(TEXT("/Script/InputCore"))
			|| ModuleName.StartsWith(TEXT("/Script/UMG"));
	}
}

void UUEOCReflectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!GEditor)
	{
		return;
	}

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		RequestDelegateHandle = TCPSubsystem->OnJsonRequestReceived.AddUObject(this, &UUEOCReflectionSubsystem::HandleRequest);
	}
}

void UUEOCReflectionSubsystem::Deinitialize()
{
	if (GEditor)
	{
		if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
		{
			if (RequestDelegateHandle.IsValid())
			{
				TCPSubsystem->OnJsonRequestReceived.Remove(RequestDelegateHandle);
				RequestDelegateHandle.Reset();
			}
		}
	}

	Super::Deinitialize();
}

void UUEOCReflectionSubsystem::HandleRequest(const FString& JsonRequest)
{
	TSharedPtr<FJsonObject> RequestJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRequest);
	if (!FJsonSerializer::Deserialize(Reader, RequestJson) || !RequestJson.IsValid())
	{
		return;
	}

	FString Type;
	FString RequestId;
	if (!RequestJson->TryGetStringField(TEXT("type"), Type)
		|| !RequestJson->TryGetStringField(TEXT("id"), RequestId))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	TSharedPtr<FJsonObject> Params;
	if (RequestJson->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr != nullptr && ParamsPtr->IsValid())
	{
		Params = *ParamsPtr;
	}
	else
	{
		Params = MakeShared<FJsonObject>();
	}

	if (Type == UEOCToolTypes::GetCppHierarchy)
	{
		HandleGetCppHierarchy(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::GetClassDetails)
	{
		HandleGetClassDetails(RequestId, Params);
	}
	else if (Type == UEOCToolTypes::SearchClasses)
	{
		HandleSearchClasses(RequestId, Params);
	}
}

void UUEOCReflectionSubsystem::HandleGetCppHierarchy(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	bool bIncludeEngine = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("bIncludeEngine"), bIncludeEngine);
	}

	constexpr int32 MaxClasses = 1000;
	int32 AddedClassCount = 0;

	TArray<TSharedPtr<FJsonValue>> ClassArray;
	ClassArray.Reserve(MaxClasses);

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (AddedClassCount >= MaxClasses)
		{
			break;
		}

		UClass* Class = *It;
		if (!Class)
		{
			continue;
		}

		const UPackage* Package = Class->GetOutermost();
		const FString ModuleName = Package ? Package->GetName() : TEXT("");
		if (!bIncludeEngine && IsDefaultEngineModule(ModuleName))
		{
			continue;
		}

		const UClass* ParentClass = Class->GetSuperClass();

		TSharedPtr<FJsonObject> ClassJson = MakeShared<FJsonObject>();
		ClassJson->SetStringField(TEXT("name"), Class->GetName());
		ClassJson->SetStringField(TEXT("parent"), ParentClass ? ParentClass->GetName() : TEXT(""));
		ClassJson->SetStringField(TEXT("module"), ModuleName);
		ClassJson->SetBoolField(TEXT("isAbstract"), Class->HasAnyClassFlags(CLASS_Abstract));
		ClassJson->SetBoolField(TEXT("isDeprecated"), Class->HasAnyClassFlags(CLASS_Deprecated));

		ClassArray.Add(MakeShared<FJsonValueObject>(ClassJson));
		++AddedClassCount;
	}

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetArrayField(TEXT("classes"), ClassArray);
	SendResponse(RequestId, UEOCToolTypes::GetCppHierarchy, DataJson);
}

void UUEOCReflectionSubsystem::HandleGetClassDetails(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString ClassName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("className"), ClassName) || ClassName.IsEmpty())
	{
		SendErrorResponse(RequestId, UEOCToolTypes::GetClassDetails, 400, TEXT("Missing required param: className"));
		return;
	}

	UClass* FoundClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* CurrentClass = *It;
		if (!CurrentClass)
		{
			continue;
		}

		if (CurrentClass->GetName().Equals(ClassName, ESearchCase::IgnoreCase)
			|| CurrentClass->GetPathName().EndsWith(ClassName, ESearchCase::IgnoreCase))
		{
			FoundClass = CurrentClass;
			break;
		}
	}

	if (!FoundClass)
	{
		SendErrorResponse(RequestId, UEOCToolTypes::GetClassDetails, 404, FString::Printf(TEXT("Class not found: %s"), *ClassName));
		return;
	}

	TArray<TSharedPtr<FJsonValue>> PropertyArray;
	for (TFieldIterator<FProperty> PropertyIt(FoundClass, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!Property)
		{
			continue;
		}

		TSharedPtr<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
		PropertyJson->SetStringField(TEXT("name"), Property->GetName());
		PropertyJson->SetStringField(TEXT("type"), Property->GetCPPType());
		PropertyJson->SetNumberField(TEXT("flags"), static_cast<double>(Property->GetPropertyFlags()));

		PropertyArray.Add(MakeShared<FJsonValueObject>(PropertyJson));
	}

	TArray<TSharedPtr<FJsonValue>> FunctionArray;
	for (TFieldIterator<UFunction> FunctionIt(FoundClass, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
	{
		UFunction* Function = *FunctionIt;
		if (!Function)
		{
			continue;
		}

		FString ReturnType = TEXT("void");
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
		{
			FProperty* ParamProperty = *ParamIt;
			if (ParamProperty && ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnType = ParamProperty->GetCPPType();
				break;
			}
		}

		TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
		FunctionJson->SetStringField(TEXT("name"), Function->GetName());
		FunctionJson->SetNumberField(TEXT("flags"), static_cast<double>(Function->FunctionFlags));
		FunctionJson->SetStringField(TEXT("returnType"), ReturnType);

		FunctionArray.Add(MakeShared<FJsonValueObject>(FunctionJson));
	}

	const UClass* ParentClass = FoundClass->GetSuperClass();
	const UPackage* Package = FoundClass->GetOutermost();

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetStringField(TEXT("name"), FoundClass->GetName());
	DataJson->SetStringField(TEXT("parent"), ParentClass ? ParentClass->GetName() : TEXT(""));
	DataJson->SetStringField(TEXT("module"), Package ? Package->GetName() : TEXT(""));
	DataJson->SetArrayField(TEXT("properties"), PropertyArray);
	DataJson->SetArrayField(TEXT("functions"), FunctionArray);
	DataJson->SetStringField(TEXT("filePath"), TEXT(""));

	SendResponse(RequestId, UEOCToolTypes::GetClassDetails, DataJson);
}

void UUEOCReflectionSubsystem::HandleSearchClasses(const FString& RequestId, TSharedPtr<FJsonObject> Params)
{
	FString Query;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("query"), Query))
	{
		Query = TEXT("");
	}

	constexpr int32 MaxResults = 20;
	int32 ResultCount = 0;

	TArray<TSharedPtr<FJsonValue>> ClassArray;
	ClassArray.Reserve(MaxResults);

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (ResultCount >= MaxResults)
		{
			break;
		}

		UClass* Class = *It;
		if (!Class)
		{
			continue;
		}

		if (!Query.IsEmpty() && !Class->GetName().Contains(Query, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const UClass* ParentClass = Class->GetSuperClass();
		const UPackage* Package = Class->GetOutermost();

		TSharedPtr<FJsonObject> ClassJson = MakeShared<FJsonObject>();
		ClassJson->SetStringField(TEXT("name"), Class->GetName());
		ClassJson->SetStringField(TEXT("parent"), ParentClass ? ParentClass->GetName() : TEXT(""));
		ClassJson->SetStringField(TEXT("module"), Package ? Package->GetName() : TEXT(""));

		ClassArray.Add(MakeShared<FJsonValueObject>(ClassJson));
		++ResultCount;
	}

	TSharedPtr<FJsonObject> DataJson = MakeShared<FJsonObject>();
	DataJson->SetArrayField(TEXT("classes"), ClassArray);
	SendResponse(RequestId, UEOCToolTypes::SearchClasses, DataJson);
}

void UUEOCReflectionSubsystem::SendResponse(const FString& RequestId, const FString& Type, TSharedPtr<FJsonObject> DataJson)
{
	if (!GEditor)
	{
		return;
	}

	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	ResponseJson->SetStringField(TEXT("id"), RequestId);
	ResponseJson->SetStringField(TEXT("type"), Type);
	ResponseJson->SetBoolField(TEXT("success"), true);
	ResponseJson->SetObjectField(TEXT("data"), DataJson.IsValid() ? DataJson : MakeShared<FJsonObject>());
	ResponseJson->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString ResponseString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem->SendJsonResponse(ResponseString);
	}
}

void UUEOCReflectionSubsystem::SendErrorResponse(const FString& RequestId, const FString& Type, int32 Code, const FString& Message)
{
	if (!GEditor)
	{
		return;
	}

	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	ResponseJson->SetStringField(TEXT("id"), RequestId);
	ResponseJson->SetStringField(TEXT("type"), Type);
	ResponseJson->SetBoolField(TEXT("success"), false);
	ResponseJson->SetNumberField(TEXT("code"), Code);
	ResponseJson->SetStringField(TEXT("message"), Message);
	ResponseJson->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);

	FString ResponseString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

	if (UUEOCTCPServerSubsystem* TCPSubsystem = GEditor->GetEditorSubsystem<UUEOCTCPServerSubsystem>())
	{
		TCPSubsystem->SendJsonResponse(ResponseString);
	}
}
