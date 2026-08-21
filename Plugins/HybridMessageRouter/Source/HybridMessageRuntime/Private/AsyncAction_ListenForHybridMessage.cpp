// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncAction_ListenForHybridMessage.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HybridMessageSubsystem.h"
#include "UObject/ScriptMacros.h"
#include "UObject/Stack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsyncAction_ListenForHybridMessage)

UAsyncAction_ListenForHybridMessage* UAsyncAction_ListenForHybridMessage::ListenForHybridMessages(
    UObject* WorldContextObject,
    FGameplayTag Channel,
    UScriptStruct* PayloadType,
    EHybridMessageMatch MatchType,
    EHybridMessagePriority Priority)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return nullptr;
    }

    UAsyncAction_ListenForHybridMessage* Action = NewObject<UAsyncAction_ListenForHybridMessage>();
    Action->WorldPtr = World;
    Action->ChannelToRegister = Channel;
    Action->MessageStructType = PayloadType;
    Action->MessageMatchType = MatchType;
    Action->MessagePriority = Priority;
    Action->RegisterWithGameInstance(World);

    return Action;
}

void UAsyncAction_ListenForHybridMessage::Activate()
{
    UWorld* World = WorldPtr.Get();
    if (!World)
    {
        SetReadyToDestroy();
        return;
    }

    if (!UHybridMessageSubsystem::HasInstance(World))
    {
        SetReadyToDestroy();
        return;
    }

    UHybridMessageSubsystem& Router = UHybridMessageSubsystem::Get(World);

    TWeakObjectPtr<UAsyncAction_ListenForHybridMessage> WeakThis(this);

    auto Callback = [WeakThis](FGameplayTag ActualChannel, const UScriptStruct* StructType, const void* Payload) -> EHybridMessageResult
    {
        if (UAsyncAction_ListenForHybridMessage* StrongThis = WeakThis.Get())
        {
            return StrongThis->HandleMessageReceived(ActualChannel, StructType, Payload);
        }
        return EHybridMessageResult::Skipped;
    };

    ListenerHandle = Router.RegisterListenerInternal(
        ChannelToRegister,
        MoveTemp(Callback),
        MessageStructType.Get(),
        MessageMatchType,
        MessagePriority,
        nullptr);
}

void UAsyncAction_ListenForHybridMessage::SetReadyToDestroy()
{
    ListenerHandle.Unregister();
    Super::SetReadyToDestroy();
}

bool UAsyncAction_ListenForHybridMessage::GetPayload(int32& OutPayload)
{
    checkNoEntry();
    return false;
}

DEFINE_FUNCTION(UAsyncAction_ListenForHybridMessage::execGetPayload)
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
        && (StructProp->Struct == P_THIS->MessageStructType.Get())
        && (P_THIS->ReceivedMessagePayloadPtr != nullptr))
    {
        StructProp->Struct->CopyScriptStruct(MessagePtr, P_THIS->ReceivedMessagePayloadPtr);
        bSuccess = true;
    }

    *(bool*)RESULT_PARAM = bSuccess;
}

EHybridMessageResult UAsyncAction_ListenForHybridMessage::HandleMessageReceived(
    FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload)
{
    if (!MessageStructType.Get() || (MessageStructType.Get() == StructType))
    {
        ReceivedMessagePayloadPtr = Payload;
        OnMessageReceived.Broadcast(this, Channel);
        ReceivedMessagePayloadPtr = nullptr;
    }

    if (!OnMessageReceived.IsBound())
    {
        SetReadyToDestroy();
    }

    return EHybridMessageResult::Handled;
}
