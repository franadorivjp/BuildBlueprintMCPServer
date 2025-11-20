#include "BlueprintMcpServerUI.h"

#include "BlueprintInspector.h"
#include "McpServer.h"
#include "Async/Async.h"
#include "Json.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Text/TextLayout.h"

#define LOCTEXT_NAMESPACE "BlueprintMcpServerUI"

void SBlueprintMcpServerPanel::Construct(const FArguments& InArgs)
{
    McpServerWeak = InArgs._McpServer;

    if (TSharedPtr<FMcpServer> Server = McpServerWeak.Pin())
    {
        LogDelegateHandle = Server->OnLog.AddSP(this, &SBlueprintMcpServerPanel::OnServerLog);
    }

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("Header", "Blueprint MCP Server"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PortLabel", "Port:"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4,0)
            [
                SAssignNew(PortTextBox, SEditableTextBox)
                .Text(FText::FromString(TEXT("9000")))
                .MinDesiredWidth(80.0f)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4,0)
            [
                SNew(SButton)
                .Text_Lambda([this]() { return IsServerRunning() ? LOCTEXT("Stop", "Stop Server") : LOCTEXT("Start", "Start Server"); })
                .OnClicked(this, &SBlueprintMcpServerPanel::OnToggleServer)
            ]
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .Padding(8,0)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return GetStatusText(); })
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .OnCheckStateChanged(this, &SBlueprintMcpServerPanel::OnToggleWrites)
                .IsChecked_Lambda([this]() { return bAllowWrites ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6,0)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("AllowWrites", "Enable write operations (unsafe)"))
                .ToolTipText(LOCTEXT("AllowWritesTip", "Allows MCP actions that create or modify Blueprints."))
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("PreviewLabel", "Test: Export Blueprint JSON"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(BlueprintPathTextBox, SEditableTextBox)
                .HintText(LOCTEXT("BlueprintPathHint", "/Game/Blueprints/BP_MyAsset.BP_MyAsset"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4,0)
            [
                SNew(SButton)
                .Text(LOCTEXT("Export", "Export JSON"))
                .OnClicked(this, &SBlueprintMcpServerPanel::OnExportJson)
            ]
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(4)
        [
            SAssignNew(LogTextBox, SMultiLineEditableTextBox)
            .IsReadOnly(true)
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(STextBlock)
            .ColorAndOpacity(FSlateColor(FLinearColor::Red))
            .Text_Lambda([this]() { return GetErrorText(); })
        ]
    ];
}

SBlueprintMcpServerPanel::~SBlueprintMcpServerPanel()
{
    if (TSharedPtr<FMcpServer> Server = McpServerWeak.Pin())
    {
        Server->OnLog.Remove(LogDelegateHandle);
    }
}

void SBlueprintMcpServerPanel::OnServerLog(const FString& Message)
{
    AppendLog(Message);
}

void SBlueprintMcpServerPanel::AppendLog(const FString& Message)
{
    LogLines.Add(Message);
    const int32 MaxLines = 200;
    if (LogLines.Num() > MaxLines)
    {
        LogLines.RemoveAt(0, LogLines.Num() - MaxLines);
    }

    if (LogTextBox.IsValid())
    {
        FString Combined = FString::Join(LogLines, TEXT("\n"));
        LogTextBox->SetText(FText::FromString(Combined));
        LogTextBox->ScrollTo(FTextLocation(MAX_int32));
    }
}

FReply SBlueprintMcpServerPanel::OnToggleServer()
{
    TSharedPtr<FMcpServer> Server = McpServerWeak.Pin();
    if (!Server.IsValid())
    {
        LastError = TEXT("Server instance missing.");
        return FReply::Handled();
    }

    if (Server->IsRunning())
    {
        Server->Stop();
        LastError.Reset();
        AppendLog(TEXT("Server stopped."));
        return FReply::Handled();
    }

    uint16 PortValue = 9000;
    if (PortTextBox.IsValid())
    {
        PortValue = (uint16)FCString::Atoi(*PortTextBox->GetText().ToString());
        if (PortValue == 0)
        {
            PortValue = 9000;
        }
    }

    FString Error;
    if (!Server->Start(PortValue, Error))
    {
        LastError = Error;
        AppendLog(Error);
        return FReply::Handled();
    }

    LastError.Reset();
    AppendLog(FString::Printf(TEXT("Server running on 127.0.0.1:%d"), PortValue));

    return FReply::Handled();
}

FReply SBlueprintMcpServerPanel::OnExportJson()
{
    TSharedPtr<FMcpServer> Server = McpServerWeak.Pin();
    if (!Server.IsValid())
    {
        LastError = TEXT("Server instance missing.");
        return FReply::Handled();
    }

    FString Path = BlueprintPathTextBox.IsValid() ? BlueprintPathTextBox->GetText().ToString() : FString();
    if (Path.IsEmpty())
    {
        LastError = TEXT("Provide a Blueprint asset path (e.g., /Game/Blueprints/BP_MyAsset.BP_MyAsset).");
        return FReply::Handled();
    }

    TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
    FString Error;
    if (!FMcpBlueprintInspector::BuildBlueprintJson(Path, JsonObj, Error))
    {
        LastError = Error;
        AppendLog(Error);
        return FReply::Handled();
    }

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(JsonObj, Writer);

    AppendLog(Output);
    LastError.Reset();
    return FReply::Handled();
}

FText SBlueprintMcpServerPanel::GetStatusText() const
{
    TSharedPtr<FMcpServer> Server = McpServerWeak.Pin();
    if (Server.IsValid() && Server->IsRunning())
    {
        return FText::FromString(FString::Printf(TEXT("Running on 127.0.0.1:%d"), Server->GetPort()));
    }
    return LOCTEXT("Stopped", "Stopped");
}

FText SBlueprintMcpServerPanel::GetErrorText() const
{
    return LastError.IsEmpty() ? FText::GetEmpty() : FText::FromString(LastError);
}

bool SBlueprintMcpServerPanel::IsServerRunning() const
{
    const TSharedPtr<FMcpServer> Server = McpServerWeak.Pin();
    return Server.IsValid() && Server->IsRunning();
}

void SBlueprintMcpServerPanel::OnToggleWrites(ECheckBoxState State)
{
    bAllowWrites = State == ECheckBoxState::Checked;
    if (TSharedPtr<FMcpServer> Server = McpServerWeak.Pin())
    {
        Server->SetAllowWrites(bAllowWrites);
    }
    AppendLog(bAllowWrites ? TEXT("Write operations enabled.") : TEXT("Write operations disabled."));
}

#undef LOCTEXT_NAMESPACE
