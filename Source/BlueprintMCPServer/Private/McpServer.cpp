#include "McpServer.h"

#include "Async/Async.h"
#include "BlueprintInspector.h"
#include "BlueprintMutator.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "HAL/PlatformProcess.h"

FMcpServer::FMcpServer()
    : bIsRunning(false)
    , Port(0)
    , HttpServerModule(nullptr)
{
}

FMcpServer::~FMcpServer()
{
    Stop();
}

bool FMcpServer::Start(uint16 InPort, FString& OutError)
{
    if (bIsRunning)
    {
        OutError = TEXT("Server already running.");
        return false;
    }

    HttpServerModule = &FHttpServerModule::Get();
    Router = HttpServerModule->GetHttpRouter(InPort);
    if (!Router.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to create HTTP router on port %d"), InPort);
        return false;
    }

    const FHttpPath Path(TEXT("/mcp"));
    FHttpRouteHandle Handle = Router->BindRoute(
        Path,
        EHttpServerRequestVerbs::VERB_POST,
        FHttpRequestHandler::CreateRaw(this, &FMcpServer::HandleRequest));

    if (!Handle)
    {
        OutError = TEXT("Failed to bind MCP route.");
        Router.Reset();
        return false;
    }

    RouteHandles.Add(Handle);

    HttpServerModule->StartAllListeners();

    Port = InPort;
    bIsRunning = true;
    Log(FString::Printf(TEXT("Server started on 127.0.0.1:%d"), Port));
    return true;
}

void FMcpServer::Stop()
{
    if (Router.IsValid())
    {
        for (FHttpRouteHandle& Handle : RouteHandles)
        {
            Router->UnbindRoute(Handle);
        }
        RouteHandles.Reset();
    }

    if (HttpServerModule)
    {
        HttpServerModule->StopAllListeners();
    }

    Router.Reset();
    HttpServerModule = nullptr;
    bIsRunning = false;
    Port = 0;
    Log(TEXT("Server stopped."));
}

bool FMcpServer::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    TArray<uint8> BodyBytes = Request.Body;
    FString BodyString;
    {
        const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(BodyBytes.GetData()), BodyBytes.Num());
        BodyString = FString(Converter.Length(), Converter.Get());
    }

    Log(FString::Printf(TEXT("Request received (%d bytes)."), BodyBytes.Num()));

    TSharedPtr<FJsonObject> RequestObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);

    if (!FJsonSerializer::Deserialize(Reader, RequestObj) || !RequestObj.IsValid())
    {
        Log(TEXT("Malformed JSON request."));
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT("Malformed JSON"), TEXT("text/plain"));
        Response->Code = EHttpServerResponseCodes::BadRequest;
        OnComplete(MoveTemp(Response));
        return true;
    }

    FString Action;
    if (!RequestObj->TryGetStringField(TEXT("action"), Action))
    {
        Log(TEXT("Missing 'action' field."));
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT("Missing 'action'"), TEXT("text/plain"));
        Response->Code = EHttpServerResponseCodes::BadRequest;
        OnComplete(MoveTemp(Response));
        return true;
    }

    const TSharedPtr<FJsonObject>* PayloadObj = nullptr;
    RequestObj->TryGetObjectField(TEXT("params"), PayloadObj);

    FString ResponsePayload;
    FString ErrorMessage;
    if (!DispatchAction(Action, PayloadObj ? *PayloadObj : MakeShared<FJsonObject>(), ResponsePayload, ErrorMessage))
    {
        Log(FString::Printf(TEXT("Action '%s' failed: %s"), *Action, *ErrorMessage));
        FString ErrorResponse = FString::Printf(TEXT("{\"error\":\"%s\"}"), *ErrorMessage.ReplaceCharWithEscapedChar());
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ErrorResponse, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::BadRequest;
        OnComplete(MoveTemp(Response));
        return true;
    }

    Log(FString::Printf(TEXT("Action '%s' succeeded."), *Action));
    TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponsePayload, TEXT("application/json"));
    Response->Code = EHttpServerResponseCodes::Ok;
    OnComplete(MoveTemp(Response));
    return true;
}

bool FMcpServer::DispatchAction(const FString& Action, const TSharedPtr<FJsonObject>& Payload, FString& OutResponse, FString& OutError) const
{
    if (Action == TEXT("list_blueprints"))
    {
        TArray<FString> Roots;
        if (Payload.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* RootArray = nullptr;
            if (Payload->TryGetArrayField(TEXT("paths"), RootArray))
            {
                for (const TSharedPtr<FJsonValue>& Value : *RootArray)
                {
                    Roots.Add(Value->AsString());
                }
            }
        }

        TArray<FString> Assets;
        if (!FMcpBlueprintInspector::ListBlueprints(Roots, Assets, OutError))
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> AssetValues;
        for (const FString& AssetPath : Assets)
        {
            AssetValues.Add(MakeShared<FJsonValueString>(AssetPath));
        }
        ResponseObj->SetArrayField(TEXT("blueprints"), AssetValues);

        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Listed %d blueprints."), AssetValues.Num()));
        return true;
    }

    if (Action == TEXT("get_blueprint_structure"))
    {
        FString AssetPath;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath))
        {
            OutError = TEXT("Missing 'asset_path'");
            return false;
        }

        TSharedRef<FJsonObject> BlueprintJson = MakeShared<FJsonObject>();
        if (!FMcpBlueprintInspector::BuildBlueprintJson(AssetPath, BlueprintJson, OutError))
        {
            return false;
        }

        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(BlueprintJson, Writer);
        Log(FString::Printf(TEXT("Exported structure for '%s'."), *AssetPath));
        return true;
    }

    if (Action == TEXT("get_references"))
    {
        FString AssetPath;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath))
        {
            OutError = TEXT("Missing 'asset_path'");
            return false;
        }

        TSharedPtr<FJsonObject> ReferencesJson;
        if (!FMcpBlueprintInspector::GetReferences(AssetPath, ReferencesJson, OutError))
        {
            return false;
        }

        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ReferencesJson.ToSharedRef(), Writer);
        Log(FString::Printf(TEXT("Fetched references for '%s'."), *AssetPath));
        return true;
    }

    // Write operations (require bAllowWrites)
    auto RequireWrite = [this, &OutError]() -> bool
    {
        if (!bAllowWrites)
        {
            OutError = TEXT("Write operations are disabled.");
            return false;
        }
        return true;
    };

    auto RunGameThread = [](TFunction<void()>&& InFunc)
    {
        if (IsInGameThread())
        {
            InFunc();
            return;
        }

        FEvent* Event = FPlatformProcess::GetSynchEventFromPool(true);
        AsyncTask(ENamedThreads::GameThread, [Event, Func = MoveTemp(InFunc)]() mutable
        {
            Func();
            Event->Trigger();
        });
        Event->Wait();
        FPlatformProcess::ReturnSynchEventToPool(Event);
    };

    if (Action == TEXT("create_blueprint"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString PackagePath;
        FString ParentClassName;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("package_path"), PackagePath))
        {
            OutError = TEXT("Missing 'package_path'");
            return false;
        }
        Payload->TryGetStringField(TEXT("parent_class"), ParentClassName);

        UClass* ParentClass = AActor::StaticClass();
        if (!ParentClassName.IsEmpty())
        {
            ParentClass = FindObject<UClass>(nullptr, *ParentClassName);
            if (!ParentClass)
            {
                OutError = FString::Printf(TEXT("Parent class '%s' not found."), *ParentClassName);
                return false;
            }
        }

        FMcpCreationResult Result;
        bool bOk = false;
        RunGameThread([&]()
        {
            Result = FMcpBlueprintMutator::CreateBlueprint(PackagePath, ParentClass);
            bOk = Result.bSuccess;
        });
        if (!bOk)
        {
            OutError = Result.Error;
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("asset_path"), Result.AssetPath);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Created Blueprint '%s'."), *Result.AssetPath));
        return true;
    }

    if (Action == TEXT("add_variable"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString VarNameStr;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetStringField(TEXT("name"), VarNameStr))
        {
            OutError = TEXT("Missing 'asset_path' or 'name'.");
            return false;
        }

        const TSharedPtr<FJsonObject>* TypeObj = nullptr;
        if (!Payload->TryGetObjectField(TEXT("type"), TypeObj) || !TypeObj || !TypeObj->IsValid())
        {
            OutError = TEXT("Missing 'type' object.");
            return false;
        }

        FEdGraphPinType PinType;
        FString Category;
        (*TypeObj)->TryGetStringField(TEXT("category"), Category);
        PinType.PinCategory = FName(*Category);
        FString SubCategory;
        (*TypeObj)->TryGetStringField(TEXT("sub_category"), SubCategory);
        if (!SubCategory.IsEmpty())
        {
            PinType.PinSubCategory = FName(*SubCategory);
        }

        bool bIsArray = false;
        bool bIsSet = false;
        bool bIsMap = false;
        (*TypeObj)->TryGetBoolField(TEXT("is_array"), bIsArray);
        (*TypeObj)->TryGetBoolField(TEXT("is_set"), bIsSet);
        (*TypeObj)->TryGetBoolField(TEXT("is_map"), bIsMap);
        PinType.ContainerType = bIsArray ? EPinContainerType::Array : (bIsSet ? EPinContainerType::Set : (bIsMap ? EPinContainerType::Map : EPinContainerType::None));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddVariable(Blueprint, FName(*VarNameStr), PinType, OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added variable '%s' to '%s'."), *VarNameStr, *AssetPath));
        return true;
    }

    if (Action == TEXT("add_function_graph"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString FunctionName;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetStringField(TEXT("name"), FunctionName))
        {
            OutError = TEXT("Missing 'asset_path' or 'name'.");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddFunctionGraph(Blueprint, FName(*FunctionName), OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added function graph '%s' to '%s'."), *FunctionName, *AssetPath));
        return true;
    }

    if (Action == TEXT("add_call_function_node"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString GraphName;
        FString FunctionPath;
        double PosX = 0;
        double PosY = 0;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("graph"), GraphName) ||
            !Payload->TryGetStringField(TEXT("function_path"), FunctionPath))
        {
            OutError = TEXT("Missing 'asset_path', 'graph', or 'function_path'.");
            return false;
        }
        Payload->TryGetNumberField(TEXT("x"), PosX);
        Payload->TryGetNumberField(TEXT("y"), PosY);

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        UFunction* TargetFunction = FindObject<UFunction>(nullptr, *FunctionPath);
        if (!TargetFunction)
        {
            OutError = TEXT("Function not found.");
            return false;
        }

        FGuid NewGuid;
        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddCallFunctionNode(Blueprint, FName(*GraphName), TargetFunction, FVector2D((float)PosX, (float)PosY), OutError, NewGuid);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("node_guid"), NewGuid.ToString(EGuidFormats::DigitsWithHyphens));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added call node '%s' to graph '%s'."), *FunctionPath, *GraphName));
        return true;
    }

    if (Action == TEXT("add_event_node"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString GraphName;
        FString EventName;
        double PosX = 0;
        double PosY = 0;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("graph"), GraphName) ||
            !Payload->TryGetStringField(TEXT("event_name"), EventName))
        {
            OutError = TEXT("Missing 'asset_path', 'graph', or 'event_name'.");
            return false;
        }
        Payload->TryGetNumberField(TEXT("x"), PosX);
        Payload->TryGetNumberField(TEXT("y"), PosY);

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        FGuid NodeGuid;
        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddEventNode(Blueprint, FName(*GraphName), FName(*EventName), FVector2D((float)PosX, (float)PosY), OutError, NodeGuid);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("node_guid"), NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added event '%s' to graph '%s'."), *EventName, *GraphName));
        return true;
    }

    if (Action == TEXT("add_input_action_event"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString GraphName;
        FString InputAction;
        FString TriggerEvent;
        double PosX = 0;
        double PosY = 0;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("graph"), GraphName) ||
            !Payload->TryGetStringField(TEXT("input_action"), InputAction) ||
            !Payload->TryGetStringField(TEXT("trigger_event"), TriggerEvent))
        {
            OutError = TEXT("Missing 'asset_path', 'graph', 'input_action', or 'trigger_event'.");
            return false;
        }
        Payload->TryGetNumberField(TEXT("x"), PosX);
        Payload->TryGetNumberField(TEXT("y"), PosY);

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        FGuid NodeGuid;
        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddInputActionEvent(Blueprint, FName(*GraphName), InputAction, FName(*TriggerEvent), FVector2D((float)PosX, (float)PosY), OutError, NodeGuid);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("node_guid"), NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added input action '%s' to graph '%s'."), *InputAction, *GraphName));
        return true;
    }

    if (Action == TEXT("add_component"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString ComponentClassPath;
        FString ComponentName;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("component_class"), ComponentClassPath) ||
            !Payload->TryGetStringField(TEXT("name"), ComponentName))
        {
            OutError = TEXT("Missing 'asset_path', 'component_class', or 'name'.");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        UClass* ComponentClass = FindObject<UClass>(nullptr, *ComponentClassPath);
        if (!ComponentClass)
        {
            OutError = TEXT("Component class not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::AddComponent(Blueprint, ComponentClass, FName(*ComponentName), OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Added component '%s' to '%s'."), *ComponentName, *AssetPath));
        return true;
    }

    if (Action == TEXT("set_pin_default"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString GraphName;
        FString NodeGuidStr;
        FString PinName;
        FString LiteralValue;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("graph"), GraphName) ||
            !Payload->TryGetStringField(TEXT("node_guid"), NodeGuidStr) ||
            !Payload->TryGetStringField(TEXT("pin_name"), PinName) ||
            !Payload->TryGetStringField(TEXT("value"), LiteralValue))
        {
            OutError = TEXT("Missing parameters for set_pin_default.");
            return false;
        }

        FGuid NodeGuid;
        if (!FGuid::Parse(NodeGuidStr, NodeGuid))
        {
            OutError = TEXT("Invalid node_guid.");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::SetPinDefault(Blueprint, FName(*GraphName), NodeGuid, PinName, LiteralValue, OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Set pin default %s on node %s"), *PinName, *NodeGuidStr));
        return true;
    }

    if (Action == TEXT("connect_pins"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        FString GraphName;
        FString FromGuidStr;
        FString ToGuidStr;
        FString FromPin;
        FString ToPin;
        if (!Payload.IsValid() ||
            !Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ||
            !Payload->TryGetStringField(TEXT("graph"), GraphName) ||
            !Payload->TryGetStringField(TEXT("from_node"), FromGuidStr) ||
            !Payload->TryGetStringField(TEXT("from_pin"), FromPin) ||
            !Payload->TryGetStringField(TEXT("to_node"), ToGuidStr) ||
            !Payload->TryGetStringField(TEXT("to_pin"), ToPin))
        {
            OutError = TEXT("Missing parameters for connect_pins.");
            return false;
        }

        FGuid FromGuid, ToGuid;
        if (!FGuid::Parse(FromGuidStr, FromGuid) || !FGuid::Parse(ToGuidStr, ToGuid))
        {
            OutError = TEXT("Invalid node GUID.");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::ConnectPins(Blueprint, FName(*GraphName), FromGuid, FromPin, ToGuid, ToPin, OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Connected pins %s:%s -> %s:%s"), *FromGuidStr, *FromPin, *ToGuidStr, *ToPin));
        return true;
    }

    if (Action == TEXT("compile_blueprint"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath))
        {
            OutError = TEXT("Missing 'asset_path'");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::Compile(Blueprint, OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Compiled Blueprint '%s'."), *AssetPath));
        return true;
    }

    if (Action == TEXT("save_blueprint"))
    {
        if (!RequireWrite())
        {
            return false;
        }

        FString AssetPath;
        if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("asset_path"), AssetPath))
        {
            OutError = TEXT("Missing 'asset_path'");
            return false;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            OutError = TEXT("Blueprint not found.");
            return false;
        }

        bool bOk = false;
        RunGameThread([&]()
        {
            bOk = FMcpBlueprintMutator::SaveBlueprint(Blueprint, OutError);
        });
        if (!bOk)
        {
            return false;
        }

        TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
        ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutResponse);
        FJsonSerializer::Serialize(ResponseObj, Writer);
        Log(FString::Printf(TEXT("Saved Blueprint '%s'."), *AssetPath));
        return true;
    }

    OutError = FString::Printf(TEXT("Unknown action '%s'"), *Action);
    return false;
}

void FMcpServer::Log(const FString& Message) const
{
    UE_LOG(LogTemp, Log, TEXT("[MCP] %s"), *Message);

    if (OnLog.IsBound())
    {
        TWeakPtr<FMcpServer> SelfWeak = const_cast<FMcpServer*>(this)->AsShared();
        AsyncTask(ENamedThreads::GameThread, [SelfWeak, Message]()
        {
            if (TSharedPtr<FMcpServer> Self = SelfWeak.Pin())
            {
                Self->OnLog.Broadcast(Message);
            }
        });
    }
}
