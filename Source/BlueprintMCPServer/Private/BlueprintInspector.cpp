#include "BlueprintInspector.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "K2Node_CallFunction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Json.h"

bool FMcpBlueprintInspector::BuildBlueprintJson(const FString& BlueprintPath, TSharedRef<FJsonObject>& OutJson, FString& OutError)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Failed to load Blueprint '%s'"), *BlueprintPath);
        return false;
    }

    OutJson->SetStringField(TEXT("asset_name"), Blueprint->GetName());
    OutJson->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());

    TArray<TSharedPtr<FJsonValue>> VarArray;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedRef<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), DescribePinType(Var.VarType));
        VarObj->SetBoolField(TEXT("is_array"), Var.VarType.IsArray());
        VarObj->SetBoolField(TEXT("is_set"), Var.VarType.IsSet());
        VarObj->SetBoolField(TEXT("is_map"), Var.VarType.IsMap());

        VarArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    OutJson->SetArrayField(TEXT("variables"), VarArray);

    TArray<TSharedPtr<FJsonValue>> GraphArray;
    if (!GatherGraphs(Blueprint, GraphArray))
    {
        OutError = TEXT("Failed to traverse Blueprint graphs.");
        return false;
    }
    OutJson->SetArrayField(TEXT("graphs"), GraphArray);

    TSharedPtr<FJsonObject> RefJson;
    if (!GetReferences(BlueprintPath, RefJson, OutError))
    {
        return false;
    }
    OutJson->SetObjectField(TEXT("references"), RefJson);

    return true;
}

bool FMcpBlueprintInspector::ListBlueprints(const TArray<FString>& Roots, TArray<FString>& OutBlueprints, FString& OutError)
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

    if (Roots.Num() > 0)
    {
        for (const FString& Root : Roots)
        {
            Filter.PackagePaths.Emplace(*Root);
        }
        Filter.bRecursivePaths = true;
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    for (const FAssetData& Data : Assets)
    {
        OutBlueprints.Add(Data.GetObjectPathString());
    }

    return true;
}

bool FMcpBlueprintInspector::GetReferences(const FString& BlueprintPath, TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
    FString PackageName;
    if (!FPackageName::TryConvertFilenameToLongPackageName(BlueprintPath, PackageName))
    {
        PackageName = BlueprintPath;
    }

    const FName PackageFName(*PackageName);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetIdentifier> Outgoing;
    AssetRegistry.GetDependencies(PackageFName, Outgoing, UE::AssetRegistry::EDependencyCategory::All);

    TArray<FAssetIdentifier> Incoming;
    AssetRegistry.GetReferencers(PackageFName, Incoming, UE::AssetRegistry::EDependencyCategory::All);

    OutJson = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> OutgoingArray;
    for (const FAssetIdentifier& Id : Outgoing)
    {
        OutgoingArray.Add(MakeShared<FJsonValueString>(Id.ToString()));
    }

    TArray<TSharedPtr<FJsonValue>> IncomingArray;
    for (const FAssetIdentifier& Id : Incoming)
    {
        IncomingArray.Add(MakeShared<FJsonValueString>(Id.ToString()));
    }

    OutJson->SetArrayField(TEXT("outgoing"), OutgoingArray);
    OutJson->SetArrayField(TEXT("incoming"), IncomingArray);
    return true;
}

bool FMcpBlueprintInspector::GatherGraphs(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutGraphs)
{
    if (!Blueprint)
    {
        return false;
    }

    auto SerializeGraph = [](UEdGraph* Graph, TArray<TSharedPtr<FJsonValue>>& TargetArray)
    {
        if (!Graph)
        {
            return;
        }

        TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());

        TArray<TSharedPtr<FJsonValue>> NodeArray;
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            SerializeNode(Node, NodeObj);
            NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }

        GraphObj->SetArrayField(TEXT("nodes"), NodeArray);
        TargetArray.Add(MakeShared<FJsonValueObject>(GraphObj));
    };

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        SerializeGraph(Graph, OutGraphs);
    }

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        SerializeGraph(Graph, OutGraphs);
    }

    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        SerializeGraph(Graph, OutGraphs);
    }

    return true;
}

void FMcpBlueprintInspector::SerializeNode(const UEdGraphNode* Node, TSharedRef<FJsonObject>& OutJson)
{
    const FString NodeId = FString::Printf(TEXT("0x%p"), Node);
    OutJson->SetStringField(TEXT("id"), NodeId);
    OutJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    OutJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

    TSharedRef<FJsonObject> PosObj = MakeShared<FJsonObject>();
    PosObj->SetNumberField(TEXT("x"), Node->NodePosX);
    PosObj->SetNumberField(TEXT("y"), Node->NodePosY);
    OutJson->SetObjectField(TEXT("position"), PosObj);

    TArray<TSharedPtr<FJsonValue>> PinArray;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin)
        {
            continue;
        }

        TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in"));
        PinObj->SetStringField(TEXT("type"), DescribePinType(Pin->PinType));

        TArray<TSharedPtr<FJsonValue>> LinkedArray;
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            if (!Linked || !Linked->GetOwningNode())
            {
                continue;
            }

            TSharedRef<FJsonObject> LinkObj = MakeShared<FJsonObject>();
            LinkObj->SetStringField(TEXT("node_id"), FString::Printf(TEXT("0x%p"), Linked->GetOwningNode()));
            LinkObj->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
            LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
        }

        PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
        PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
    }

    OutJson->SetArrayField(TEXT("pins"), PinArray);
}

FString FMcpBlueprintInspector::DescribePinType(const FEdGraphPinType& PinType)
{
    FString Result = PinType.PinCategory.ToString();

    if (PinType.PinSubCategoryObject.IsValid())
    {
        Result += FString::Printf(TEXT(":%s"), *PinType.PinSubCategoryObject->GetName());
    }
    else if (!PinType.PinSubCategory.IsNone())
    {
        Result += FString::Printf(TEXT(":%s"), *PinType.PinSubCategory.ToString());
    }

    if (PinType.IsContainer())
    {
        if (PinType.IsArray())
        {
            Result += TEXT("[]");
        }
        else if (PinType.IsSet())
        {
            Result += TEXT("<set>");
        }
        else if (PinType.IsMap())
        {
            Result += TEXT("<map>");
        }
    }

    return Result;
}
