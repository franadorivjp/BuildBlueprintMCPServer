#include "BlueprintMCPServerModule.h"

#include "BlueprintMcpServerUI.h"
#include "McpServer.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

static const FName BlueprintMcpTabName(TEXT("BlueprintMcpServerTab"));

#define LOCTEXT_NAMESPACE "FBlueprintMCPServerModule"

void FBlueprintMCPServerModule::StartupModule()
{
    McpServer = MakeShared<FMcpServer>();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        BlueprintMcpTabName,
        FOnSpawnTab::CreateRaw(this, &FBlueprintMCPServerModule::SpawnMainTab))
        .SetDisplayName(LOCTEXT("TabTitle", "Blueprint MCP Server"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    RegisterMenus();
}

void FBlueprintMCPServerModule::ShutdownModule()
{
    if (McpServer.IsValid())
    {
        McpServer->Stop();
        McpServer.Reset();
    }

    UnregisterMenus();
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BlueprintMcpTabName);
}

TSharedRef<SDockTab> FBlueprintMCPServerModule::SpawnMainTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SBlueprintMcpServerPanel)
            .McpServer(McpServer)
        ];
}

void FBlueprintMCPServerModule::RegisterMenus()
{
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
        {
            FToolMenuOwnerScoped OwnerScoped(this);

            UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
            FToolMenuSection& Section = ToolsMenu->AddSection(
                "BlueprintMcpSection",
                LOCTEXT("BlueprintMcpSection", "Blueprint MCP Server"));

            Section.AddMenuEntry(
                "OpenBlueprintMcpServerTab",
                LOCTEXT("OpenBlueprintMcpServerTab", "Blueprint MCP Server"),
                LOCTEXT("OpenBlueprintMcpServerTab_Tooltip", "Open the Blueprint MCP Server control panel."),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([]()
                {
                    FGlobalTabmanager::Get()->TryInvokeTab(BlueprintMcpTabName);
                })));
        }));
}

void FBlueprintMCPServerModule::UnregisterMenus()
{
    UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintMCPServerModule, BlueprintMCPServer)
