#pragma once

#include "Modules/ModuleManager.h"

class FMcpServer;

class FBlueprintMCPServerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void UnregisterMenus();
    TSharedRef<class SDockTab> SpawnMainTab(const class FSpawnTabArgs& Args);

    TSharedPtr<FMcpServer> McpServer;
    FDelegateHandle MenuExtenderHandle;
};
