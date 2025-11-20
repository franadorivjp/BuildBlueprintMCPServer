#pragma once

#include "CoreMinimal.h"

class UBlueprint;

class FMcpBlueprintInspector
{
public:
    static bool BuildBlueprintJson(const FString& BlueprintPath, TSharedRef<class FJsonObject>& OutJson, FString& OutError);
    static bool ListBlueprints(const TArray<FString>& Roots, TArray<FString>& OutBlueprints, FString& OutError);
    static bool GetReferences(const FString& BlueprintPath, TSharedPtr<FJsonObject>& OutJson, FString& OutError);

private:
    static bool GatherGraphs(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutGraphs);
    static void SerializeNode(const class UEdGraphNode* Node, TSharedRef<class FJsonObject>& OutJson);
    static FString DescribePinType(const struct FEdGraphPinType& PinType);
};
