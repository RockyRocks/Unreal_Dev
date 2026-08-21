// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "K2Node_AsyncAction.h"

#include "K2Node_AsyncAction_ListenForHybridMessages.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class FMulticastDelegateProperty;
class FString;
class UEdGraph;
class UEdGraphPin;
class UObject;

/**
 * Custom Blueprint graph node for the UAsyncAction_ListenForHybridMessage async action.
 *
 * Responsibilities:
 *  - Hides the internal ProxyObject delegate output pin
 *  - Creates a wildcard Payload output pin
 *  - Syncs the Payload pin type with the PayloadType input
 *  - Generates the GetPayload() intermediate call during compilation
 */
UCLASS()
class UK2Node_AsyncAction_ListenForHybridMessages : public UK2Node_AsyncAction
{
    GENERATED_BODY()

    //~ UEdGraphNode interface
    virtual void PostReconstructNode() override;
    virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
    virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
    //~ End of UEdGraphNode interface

    //~ UK2Node interface
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
    virtual void AllocateDefaultPins() override;
    //~ End of UK2Node interface

protected:
    virtual bool HandleDelegates(
        const TArray<FBaseAsyncTaskHelper::FOutputPinAndLocalVariable>& VariableOutputs,
        UEdGraphPin* ProxyObjectPin,
        UEdGraphPin*& InOutLastThenPin,
        UEdGraph* SourceGraph,
        FKismetCompilerContext& CompilerContext) override;

private:
    bool HandlePayloadImplementation(
        FMulticastDelegateProperty* CurrentProperty,
        const FBaseAsyncTaskHelper::FOutputPinAndLocalVariable& ProxyObjectVar,
        const FBaseAsyncTaskHelper::FOutputPinAndLocalVariable& PayloadVar,
        const FBaseAsyncTaskHelper::FOutputPinAndLocalVariable& ActualChannelVar,
        UEdGraphPin*& InOutLastActivatedThenPin,
        UEdGraph* SourceGraph,
        FKismetCompilerContext& CompilerContext);

    void RefreshOutputPayloadType();

    UEdGraphPin* GetPayloadPin() const;
    UEdGraphPin* GetPayloadTypePin() const;
    UEdGraphPin* GetOutputChannelPin() const;
};
