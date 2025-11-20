#include "BlueprintMutator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BlueprintEditorSettings.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Factories/BlueprintFactory.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_InputAction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Components/ActorComponent.h"
#include "KismetCompilerModule.h"

FMcpCreationResult FMcpBlueprintMutator::CreateBlueprint(const FString& PackagePath, UClass* ParentClass)
{
    FMcpCreationResult Result;
    if (PackagePath.IsEmpty())
    {
        Result.Error = TEXT("PackagePath is empty.");
        return Result;
    }

    if (!PackagePath.StartsWith(TEXT("/")))
    {
        Result.Error = TEXT("Package path must start with '/'. Use long package names like /Game/MyFolder/BP_Name.");
        return Result;
    }

    if (!FPackageName::IsValidLongPackageName(PackagePath))
    {
        Result.Error = TEXT("Package path is not a valid long package name (e.g., /Game/MyFolder/BP_Name).");
        return Result;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
    const FString PackageName = FPackageName::GetLongPackagePath(PackagePath);

    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = ParentClass ? ParentClass : AActor::StaticClass();

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), Factory);
    if (!NewAsset)
    {
        Result.Error = TEXT("Failed to create Blueprint asset.");
        return Result;
    }

    FAssetRegistryModule::AssetCreated(NewAsset);
    NewAsset->MarkPackageDirty();

    Result.bSuccess = true;
    Result.AssetPath = NewAsset->GetPathName();
    return Result;
}

bool FMcpBlueprintMutator::AddVariable(UBlueprint* Blueprint, const FName& VarName, const FEdGraphPinType& PinType, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    if (VarName.IsNone())
    {
        OutError = TEXT("Variable name is empty.");
        return false;
    }

    const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType);
    if (!bAdded)
    {
        OutError = TEXT("Failed to add variable (maybe duplicate?).");
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::AddFunctionGraph(UBlueprint* Blueprint, const FName& FunctionName, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    if (FunctionName.IsNone())
    {
        OutError = TEXT("Function name is empty.");
        return false;
    }

    if (UEdGraph* Existing = FindGraph(Blueprint, FunctionName))
    {
        OutError = TEXT("Graph already exists.");
        return false;
    }

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
    {
        OutError = TEXT("Failed to create graph.");
        return false;
    }

    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, /*bIsUserCreated=*/true, nullptr);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::AddCallFunctionNode(UBlueprint* Blueprint, const FName& GraphName, UFunction* TargetFunction, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid)
{
    if (!Blueprint || !TargetFunction)
    {
        OutError = TEXT("Blueprint or Function is null.");
        return false;
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = TEXT("Graph not found.");
        return false;
    }

    UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
    Node->CreateNewGuid();
    Node->SetFlags(RF_Transactional);
    Node->SetFromFunction(TargetFunction);
    Node->AllocateDefaultPins();
    Node->NodePosX = (int32)Position.X;
    Node->NodePosY = (int32)Position.Y;

    Graph->AddNode(Node, /*bFromUI=*/true, /*bSelectNewNode=*/false);
    OutNodeGuid = Node->NodeGuid;

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::AddEventNode(UBlueprint* Blueprint, const FName& GraphName, const FName& EventName, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = TEXT("Graph not found.");
        return false;
    }

    UK2Node_Event* Node = NewObject<UK2Node_Event>(Graph);
    Node->CreateNewGuid();
    Node->SetFlags(RF_Transactional);
    Node->EventReference.SetExternalMember(EventName, Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass);
    Node->CustomFunctionName = EventName;
    Node->AllocateDefaultPins();
    Node->NodePosX = (int32)Position.X;
    Node->NodePosY = (int32)Position.Y;

    Graph->AddNode(Node, true, false);
    OutNodeGuid = Node->NodeGuid;

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::AddInputActionEvent(UBlueprint* Blueprint, const FName& GraphName, const FString& InputActionPath, const FName& TriggerEventName, const FVector2D& Position, FString& OutError, FGuid& OutNodeGuid)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = TEXT("Graph not found.");
        return false;
    }

    UObject* InputActionObj = LoadObject<UObject>(nullptr, *InputActionPath);
    if (!InputActionObj)
    {
        OutError = TEXT("InputAction not found.");
        return false;
    }

    UK2Node_InputAction* Node = NewObject<UK2Node_InputAction>(Graph);
    Node->CreateNewGuid();
    Node->SetFlags(RF_Transactional);
    Node->InputActionName = FName(*InputActionPath);
    Node->bConsumeInput = false;
    Node->bExecuteWhenPaused = false;
    Node->bOverrideParentBinding = false;
    Node->NodePosX = (int32)Position.X;
    Node->NodePosY = (int32)Position.Y;
    Node->AllocateDefaultPins();
    Graph->AddNode(Node, true, false);
    OutNodeGuid = Node->NodeGuid;

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::AddComponent(UBlueprint* Blueprint, UClass* ComponentClass, const FName& ComponentName, FString& OutError)
{
    if (!Blueprint || !ComponentClass)
    {
        OutError = TEXT("Blueprint or ComponentClass is null.");
        return false;
    }

    UActorComponent* Template = NewObject<UActorComponent>(Blueprint, ComponentClass, ComponentName, RF_Transactional | RF_Public);
    if (!Template)
    {
        OutError = TEXT("Failed to create component template.");
        return false;
    }

    Blueprint->ComponentTemplates.Add(Template);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::SetPinDefault(UBlueprint* Blueprint, const FName& GraphName, const FGuid& NodeGuid, const FString& PinName, const FString& LiteralValue, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = TEXT("Graph not found.");
        return false;
    }

    UEdGraphNode* TargetNode = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid == NodeGuid)
        {
            TargetNode = Node;
            break;
        }
    }

    if (!TargetNode)
    {
        OutError = TEXT("Node not found.");
        return false;
    }

    for (UEdGraphPin* Pin : TargetNode->Pins)
    {
        if (Pin && Pin->PinName.ToString() == PinName)
        {
            Pin->DefaultValue = LiteralValue;
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            return true;
        }
    }

    OutError = TEXT("Pin not found.");
    return false;
}

bool FMcpBlueprintMutator::ConnectPins(UBlueprint* Blueprint, const FName& GraphName, const FGuid& FromNode, const FString& FromPin, const FGuid& ToNode, const FString& ToPin, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    UEdGraph* Graph = FindGraph(Blueprint, GraphName);
    if (!Graph)
    {
        OutError = TEXT("Graph not found.");
        return false;
    }

    UEdGraphNode* FromNodePtr = nullptr;
    UEdGraphNode* ToNodePtr = nullptr;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        if (Node->NodeGuid == FromNode)
        {
            FromNodePtr = Node;
        }
        if (Node->NodeGuid == ToNode)
        {
            ToNodePtr = Node;
        }
    }

    if (!FromNodePtr || !ToNodePtr)
    {
        OutError = TEXT("Node(s) not found by GUID.");
        return false;
    }

    UEdGraphPin* FromPinPtr = nullptr;
    UEdGraphPin* ToPinPtr = nullptr;

    for (UEdGraphPin* Pin : FromNodePtr->Pins)
    {
        if (Pin && Pin->PinName.ToString() == FromPin)
        {
            FromPinPtr = Pin;
            break;
        }
    }

    for (UEdGraphPin* Pin : ToNodePtr->Pins)
    {
        if (Pin && Pin->PinName.ToString() == ToPin)
        {
            ToPinPtr = Pin;
            break;
        }
    }

    if (!FromPinPtr || !ToPinPtr)
    {
        OutError = TEXT("Pin(s) not found.");
        return false;
    }

    FromPinPtr->MakeLinkTo(ToPinPtr);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FMcpBlueprintMutator::Compile(UBlueprint* Blueprint, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == EBlueprintStatus::BS_Error)
    {
        OutError = TEXT("Compile failed.");
        return false;
    }
    return true;
}

bool FMcpBlueprintMutator::SaveBlueprint(UBlueprint* Blueprint, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    UPackage* Package = Blueprint->GetOutermost();
    if (!Package)
    {
        OutError = TEXT("Package not found.");
        return false;
    }

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    if (!UPackage::SavePackage(Package, Blueprint, *PackageFilename, SaveArgs))
    {
        OutError = TEXT("Failed to save package.");
        return false;
    }

    return true;
}

UEdGraph* FMcpBlueprintMutator::FindGraph(UBlueprint* Blueprint, const FName& GraphName)
{
    if (!Blueprint)
    {
        return nullptr;
    }

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph && Graph->GetFName() == GraphName)
        {
            return Graph;
        }
    }

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph && Graph->GetFName() == GraphName)
        {
            return Graph;
        }
    }

    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph && Graph->GetFName() == GraphName)
        {
            return Graph;
        }
    }

    return nullptr;
}
