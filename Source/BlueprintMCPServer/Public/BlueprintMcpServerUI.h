#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FMcpServer;

class SBlueprintMcpServerPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBlueprintMcpServerPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FMcpServer>, McpServer)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SBlueprintMcpServerPanel();

private:
    void OnServerLog(const FString& Message);
    void AppendLog(const FString& Message);

    FReply OnToggleServer();
    FReply OnExportJson();
    void OnToggleWrites(ECheckBoxState State);

    FText GetStatusText() const;
    FText GetErrorText() const;
    bool IsServerRunning() const;

    TWeakPtr<FMcpServer> McpServerWeak;
    TSharedPtr<class SEditableTextBox> PortTextBox;
    TSharedPtr<class SEditableTextBox> BlueprintPathTextBox;
    TSharedPtr<class SMultiLineEditableTextBox> LogTextBox;

    FString LastError;
    TArray<FString> LogLines;
    FDelegateHandle LogDelegateHandle;
    bool bAllowWrites = false;
};
