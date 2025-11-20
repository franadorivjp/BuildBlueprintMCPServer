#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UK2Node_CallFunction;

struct FMcpCreationResult
{
    bool bSuccess = false;
    FString Error;
    FString AssetPath;
};

class FMcpBlueprintMutator
{
public:
    static FMcpCreationResult CreateBlueprint(const FString& PackagePath, UClass* ParentClass);
    static bool AddVariable(UBlueprint* Blueprint, const FName& VarName, const FEdGraphPinType& PinType, FString& OutError);
    static bool AddFunctionGraph(UBlueprint* Blueprint, const FName& FunctionName, FString& OutError);
    static bool AddCallFunctionNode(UBlueprint* Blueprint, const FName& GraphName, UFunction* TargetFunction, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid);
    static bool AddEventNode(UBlueprint* Blueprint, const FName& GraphName, const FName& EventName, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid);
    static bool AddInputActionEvent(UBlueprint* Blueprint, const FName& GraphName, const FString& InputActionPath, const FName& TriggerEventName, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid);
    static bool AddComponent(UBlueprint* Blueprint, UClass* ComponentClass, const FName& ComponentName, FString& OutError);
    static bool SetPinDefault(UBlueprint* Blueprint, const FName& GraphName, const FGuid& NodeGuid, const FString& PinName, const FString& LiteralValue, FString& OutError);
    static bool ConnectPins(UBlueprint* Blueprint, const FName& GraphName, const FGuid& FromNode, const FString& FromPin, const FGuid& ToNode, const FString& ToPin, FString& OutError);
    static bool Compile(UBlueprint* Blueprint, FString& OutError);
    static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);

private:
    static UEdGraph* FindGraph(UBlueprint* Blueprint, const FName& GraphName);
};
