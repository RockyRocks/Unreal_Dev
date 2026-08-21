// Copyright Epic Games, Inc. All Rights Reserved.

#include "HybridSignalComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/ScriptMacros.h"
#include "UObject/Stack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HybridSignalComponent)

UHybridSignalComponent::UHybridSignalComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UHybridSignalComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UHybridSignalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopListeningAll();
    Super::EndPlay(EndPlayReason);
}

void UHybridSignalComponent::StartListening(
    FGameplayTag Channel,
    UScriptStruct* PayloadType,
    EHybridMessageMatch MatchType,
    EHybridMessagePriority Priority)
{
    if (!Channel.IsValid())
    {
        UE_LOG(LogHybridMessage, Warning, TEXT("HybridSignalComponent::StartListening called with invalid channel"));
        return;
    }

    // If already listening on this channel, unregister first
    if (ActiveListeners.Contains(Channel))
    {
        StopListening(Channel);
    }

    if (!UHybridMessageSubsystem::HasInstance(this))
    {
        UE_LOG(LogHybridMessage, Warning, TEXT("HybridSignalComponent::StartListening — no HybridMessageSubsystem available"));
        return;
    }

    UHybridMessageSubsystem& Router = UHybridMessageSubsystem::Get(this);

    TWeakObjectPtr<UHybridSignalComponent> WeakThis(this);

    auto Callback = [WeakThis](FGameplayTag ActualChannel, const UScriptStruct* StructType, const void* Payload) -> EHybridMessageResult
    {
        if (UHybridSignalComponent* StrongThis = WeakThis.Get())
        {
            StrongThis->HandleMessageInternal(ActualChannel, StructType, Payload);
        }
        return EHybridMessageResult::Handled;
    };

    FHybridMessageListenerHandle Handle = Router.RegisterListenerInternal(
        Channel, MoveTemp(Callback), PayloadType, MatchType, Priority, nullptr);

    FActiveListener& Entry = ActiveListeners.Add(Channel);
    Entry.Handle = Handle;
    Entry.PayloadType = PayloadType;
}

void UHybridSignalComponent::StopListening(FGameplayTag Channel)
{
    if (FActiveListener* Entry = ActiveListeners.Find(Channel))
    {
        Entry->Handle.Unregister();
        ActiveListeners.Remove(Channel);
    }
}

void UHybridSignalComponent::StopListeningAll()
{
    for (auto& Pair : ActiveListeners)
    {
        Pair.Value.Handle.Unregister();
    }
    ActiveListeners.Reset();
}

void UHybridSignalComponent::HandleMessageInternal(
    FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload)
{
    LastReceivedPayload = Payload;
    LastReceivedPayloadType = StructType;

    OnMessageReceived.Broadcast(Channel, const_cast<UScriptStruct*>(StructType));

    LastReceivedPayload = nullptr;
    LastReceivedPayloadType = nullptr;
}

// -------------------------------------------------------------------
// BroadcastMessage (CustomThunk)
// -------------------------------------------------------------------

void UHybridSignalComponent::BroadcastMessage(FGameplayTag Channel, const int32& Message)
{
    checkNoEntry();
}

DEFINE_FUNCTION(UHybridSignalComponent::execBroadcastMessage)
{
    P_GET_STRUCT(FGameplayTag, Channel);

    Stack.MostRecentPropertyAddress = nullptr;
    Stack.StepCompiledIn<FStructProperty>(nullptr);
    void* MessagePtr = Stack.MostRecentPropertyAddress;
    FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

    P_FINISH;

    if (ensure((StructProp != nullptr) && (StructProp->Struct != nullptr) && (MessagePtr != nullptr)))
    {
        if (UHybridMessageSubsystem::HasInstance(P_THIS))
        {
            UHybridMessageSubsystem::Get(P_THIS).BroadcastMessageInternal(Channel, StructProp->Struct, MessagePtr);
        }
    }
}

// -------------------------------------------------------------------
// GetLastPayload (CustomThunk)
// -------------------------------------------------------------------

bool UHybridSignalComponent::GetLastPayload(int32& OutPayload)
{
    checkNoEntry();
    return false;
}

DEFINE_FUNCTION(UHybridSignalComponent::execGetLastPayload)
{
    Stack.MostRecentPropertyAddress = nullptr;
    Stack.StepCompiledIn<FStructProperty>(nullptr);
    void* MessagePtr = Stack.MostRecentPropertyAddress;
    FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);
    P_FINISH;

    bool bSuccess = false;

    if ((StructProp != nullptr)
        && (StructProp->Struct != nullptr)
        && (MessagePtr != nullptr)
        && P_THIS->LastReceivedPayloadType.IsValid()
        && (StructProp->Struct == P_THIS->LastReceivedPayloadType.Get())
        && (P_THIS->LastReceivedPayload != nullptr))
    {
        StructProp->Struct->CopyScriptStruct(MessagePtr, P_THIS->LastReceivedPayload);
        bSuccess = true;
    }

    *(bool*)RESULT_PARAM = bSuccess;
}
