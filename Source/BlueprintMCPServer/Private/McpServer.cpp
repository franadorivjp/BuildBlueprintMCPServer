#include "McpServer.h"

#include "Async/Async.h"
#include "BlueprintInspector.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Json.h"
#include "JsonUtilities.h"

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

    OutError = FString::Printf(TEXT("Unknown action '%s'"), *Action);
    return false;
}

void FMcpServer::Log(const FString& Message) const
{
    UE_LOG(LogTemp, Log, TEXT("[MCP] %s"), *Message);

    if (OnLog.IsBound())
    {
        TWeakPtr<const FMcpServer> SelfWeak = AsShared();
        AsyncTask(ENamedThreads::GameThread, [SelfWeak, Message]()
        {
            if (TSharedPtr<const FMcpServer> Self = SelfWeak.Pin())
            {
                Self->OnLog.Broadcast(Message);
            }
        });
    }
}
