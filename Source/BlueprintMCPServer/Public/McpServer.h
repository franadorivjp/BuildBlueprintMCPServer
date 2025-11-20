#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpRequestHandler.h"
#include "HttpResultCallback.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FMcpLogDelegate, const FString& /*Message*/);

class FHttpServerModule;
class IHttpRouter;

class FMcpServer : public TSharedFromThis<FMcpServer>
{
public:
    FMcpServer();
    ~FMcpServer();

    bool Start(uint16 InPort, FString& OutError);
    void Stop();

    bool IsRunning() const { return bIsRunning; }
    uint16 GetPort() const { return Port; }
    void SetAllowWrites(bool bInAllowWrites) { bAllowWrites = bInAllowWrites; }

    FMcpLogDelegate OnLog;

private:
    bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
    bool DispatchAction(const FString& Action, const TSharedPtr<FJsonObject>& Payload, FString& OutResponse, FString& OutError) const;
    void Log(const FString& Message) const;
    bool bAllowWrites = false;

    bool bIsRunning;
    uint16 Port;

    class FHttpServerModule* HttpServerModule;
    TSharedPtr<class IHttpRouter> Router;
    TArray<FHttpRouteHandle> RouteHandles;
};
