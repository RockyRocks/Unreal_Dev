// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/CancellableAsyncAction.h"
#include "HybridMessageSubsystem.h"
#include "HybridMessageTypes.h"

#include "AsyncAction_ListenForHybridMessage.generated.h"

#define HYBRID_API HYBRIDMESSAGERUNTIME_API

class UScriptStruct;
class UWorld;
struct FFrame;

/**
 * Delegate broadcast when the async listener receives a message.
 * ProxyObject is used internally by the K2Node to call GetPayload().
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FAsyncHybridMessageDelegate,
    UAsyncAction_ListenForHybridMessage*, ProxyObject,
    FGameplayTag, ActualChannel);

/**
 * Blueprint async action for listening to Tier 1 (hub) messages.
 *
 * This node creates a persistent listener that fires its delegate
 * each time a matching message arrives. Use the GetPayload() output
 * to access the message struct.
 *
 * Cancelling the action or destroying the owning Blueprint actor
 * automatically unregisters the listener.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(HasDedicatedAsyncNode))
class UAsyncAction_ListenForHybridMessage : public UCancellableAsyncAction
{
    GENERATED_BODY()

public:
    /**
     * Asynchronously waits for a hybrid message on the specified channel.
     *
     * @param Channel       The message channel to listen on
     * @param PayloadType   The USTRUCT type of the expected message payload
     * @param MatchType     Exact or partial (hierarchical) channel matching
     * @param Priority      Listener priority (First runs before Last)
     */
    UFUNCTION(BlueprintCallable, Category = Messaging, meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static HYBRID_API UAsyncAction_ListenForHybridMessage* ListenForHybridMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        UScriptStruct* PayloadType,
        EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch,
        EHybridMessagePriority Priority = EHybridMessagePriority::Normal);

    /**
     * Copy the received message payload into the specified wildcard struct.
     * The struct type must match the PayloadType specified at registration.
     */
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "Messaging", meta = (CustomStructureParam = "OutPayload"))
    HYBRID_API bool GetPayload(UPARAM(ref) int32& OutPayload);

    DECLARE_FUNCTION(execGetPayload);

    HYBRID_API virtual void Activate() override;
    HYBRID_API virtual void SetReadyToDestroy() override;

public:
    UPROPERTY(BlueprintAssignable)
    FAsyncHybridMessageDelegate OnMessageReceived;

private:
    EHybridMessageResult HandleMessageReceived(FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload);

private:
    const void* ReceivedMessagePayloadPtr = nullptr;

    TWeakObjectPtr<UWorld> WorldPtr;
    FGameplayTag ChannelToRegister;
    TWeakObjectPtr<UScriptStruct> MessageStructType = nullptr;
    EHybridMessageMatch MessageMatchType = EHybridMessageMatch::ExactMatch;
    EHybridMessagePriority MessagePriority = EHybridMessagePriority::Normal;

    FHybridMessageListenerHandle ListenerHandle;
};

#undef HYBRID_API
