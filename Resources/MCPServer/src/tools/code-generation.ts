import type { Tool } from '@modelcontextprotocol/sdk/types.js';
import type { UETCPClient } from '../tcp-client.js';
import { UE_TOOL_TYPES } from '../types/constants.js';

type ToolResult = { content: Array<{ type: 'text'; text: string }> };

const GET_CODE_TEMPLATE = 'get_code_template';

const CLASS_TYPES = [
  'Actor',
  'Component',
  'GameMode',
  'Widget',
  'AnimInstance',
  'Interface',
  'Struct',
  'Enum',
] as const;

type ClassType = (typeof CLASS_TYPES)[number];

export const TOOL_DEFINITIONS: Tool[] = [
  {
    name: UE_TOOL_TYPES.GENERATE_CODE,
    description:
      'Send a code generation request to UE Editor; UE handles confirmation and file write',
    inputSchema: {
      type: 'object' as const,
      properties: {
        filePath: {
          type: 'string',
          description: 'Project-relative file path to generate',
        },
        content: {
          type: 'string',
          description: 'Full file content to generate',
        },
        description: {
          type: 'string',
          description: 'Human-readable reason shown in UE confirmation UI',
        },
      },
      required: ['filePath', 'content', 'description'],
    },
  },
  {
    name: GET_CODE_TEMPLATE,
    description: 'Get a UE C++ code template for a common class type',
    inputSchema: {
      type: 'object' as const,
      properties: {
        classType: {
          type: 'string',
          enum: [...CLASS_TYPES],
          description:
            'Template type: Actor, Component, GameMode, Widget, AnimInstance, Interface, Struct, Enum',
        },
      },
      required: ['classType'],
    },
  },
];

function getTemplate(classType: ClassType): string {
  switch (classType) {
    case 'Actor':
      return `#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyActor.generated.h"

UCLASS()
class MYPROJECT_API AMyActor : public AActor
{
    GENERATED_BODY()

public:
    AMyActor();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
};
`;

    case 'Component':
      return `#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyActorComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPROJECT_API UMyActorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMyActorComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
`;

    case 'GameMode':
      return `#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MyGameMode.generated.h"

UCLASS()
class MYPROJECT_API AMyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMyGameMode();

protected:
    virtual void BeginPlay() override;
};
`;

    case 'Widget':
      return `#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyUserWidget.generated.h"

UCLASS()
class MYPROJECT_API UMyUserWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="UI")
    void InitializeWidget();
};
`;

    case 'AnimInstance':
      return `#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MyAnimInstance.generated.h"

UCLASS()
class MYPROJECT_API UMyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
`;

    case 'Interface':
      return `#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MyInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UMyInterface : public UInterface
{
    GENERATED_BODY()
};

class MYPROJECT_API IMyInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interface")
    void ExecuteAction();
};
`;

    case 'Struct':
      return `#pragma once

#include "CoreMinimal.h"
#include "MyData.generated.h"

USTRUCT(BlueprintType)
struct MYPROJECT_API FMyData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
    int32 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Data")
    FString Name = TEXT("Default");
};
`;

    case 'Enum':
      return `#pragma once

#include "CoreMinimal.h"
#include "MyEnum.generated.h"

UENUM(BlueprintType)
enum class EMyEnum : uint8
{
    None UMETA(DisplayName = "None"),
    OptionA UMETA(DisplayName = "Option A"),
    OptionB UMETA(DisplayName = "Option B")
};
`;

    default:
      return '';
  }
}

export async function handleToolCall(
  name: string,
  args: Record<string, unknown>,
  tcpClient: UETCPClient,
): Promise<ToolResult | null> {
  switch (name) {
    case UE_TOOL_TYPES.GENERATE_CODE: {
      try {
        const response = await tcpClient.sendRequest(name, args);
        if (!response.success) {
          return {
            content: [
              {
                type: 'text' as const,
                text: `Error: ${response.error?.message ?? 'Unknown UE error'}`,
              },
            ],
          };
        }
        return {
          content: [
            {
              type: 'text' as const,
              text: JSON.stringify(response.data, null, 2),
            },
          ],
        };
      } catch (err) {
        return {
          content: [
            {
              type: 'text' as const,
              text: `UE not connected: ${err}`,
            },
          ],
        };
      }
    }

    case GET_CODE_TEMPLATE: {
      const classType = args.classType;
      if (typeof classType !== 'string' || !CLASS_TYPES.includes(classType as ClassType)) {
        return {
          content: [
            {
              type: 'text' as const,
              text: `Error: classType must be one of: ${CLASS_TYPES.join(', ')}`,
            },
          ],
        };
      }

      return {
        content: [
          {
            type: 'text' as const,
            text: getTemplate(classType as ClassType),
          },
        ],
      };
    }

    default:
      return null;
  }
}
