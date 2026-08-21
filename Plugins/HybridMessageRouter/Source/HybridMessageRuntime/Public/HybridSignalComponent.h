// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "HybridMessageSubsystem.h"
#include "HybridMessageTypes.h"

#include "HybridSignalComponent.generated.h"

#define HYBRID_API HYBRIDMESSAGERUNTIME_API

/**
 * Delegate fired when this component receives a message from the Tier 1 hub.
 * Use GetLastPayload() in the handler to retrieve the struct data.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnHybridComponentMessageReceived,
    FGameplayTag, Channel,
    UScriptStruct*, PayloadType);

/**
 * ActorComponent that bridges the Tier 1 message hub into the actor/component world.
 *
 * Attach this to any actor to allow Blueprint-driven message listening
 * on multiple channels simultaneously. The component manages listener
 * lifetimes automatically — all listeners are unregistered when the
 * component is destroyed.
 *
 * For C++ actors that want direct signal-slot performance, prefer
 * THybridSignal / THybridSlot directly. This component is designed
 * for Blueprint ergonomics and dynamic (runtime-configured) listeners.
 */
UCLASS(BlueprintType, ClassGroup=Messaging, meta=(BlueprintSpawnableComponent))
class HYBRIDMESSAGERUNTIME_API UHybridSignalComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHybridSignalComponent(const FObjectInitializer& ObjectInitializer);

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * Start listening for messages on the given channel.
     * When a message arrives, OnMessageReceived fires and the payload
     * is available via GetLastPayload().
     */
    UFUNCTION(BlueprintCallable, Category = "Messaging")
    void StartListening(FGameplayTag Channel, UScriptStruct* PayloadType,
        EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch,
        EHybridMessagePriority Priority = EHybridMessagePriority::Normal);

    /**
     * Stop listening on a specific channel. Safe to call if not currently listening.
     */
    UFUNCTION(BlueprintCallable, Category = "Messaging")
    void StopListening(FGameplayTag Channel);

    /**
     * Stop listening on all channels this component is registered for.
     */
    UFUNCTION(BlueprintCallable, Category = "Messaging")
    void StopListeningAll();

    /**
     * Broadcast a message through the Tier 1 hub. Uses CustomThunk
     * for arbitrary struct support.
     */
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "Messaging",
        meta=(CustomStructureParam="Message", AllowAbstract="false", DisplayName="Broadcast Message"))
    void BroadcastMessage(FGameplayTag Channel, const int32& Message);

    DECLARE_FUNCTION(execBroadcastMessage);

    /**
     * Copy the last received payload into the target struct.
     * Only valid during the OnMessageReceived callback.
     */
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "Messaging",
        meta=(CustomStructureParam="OutPayload"))
    bool GetLastPayload(UPARAM(ref) int32& OutPayload);

    DECLARE_FUNCTION(execGetLastPayload);

    UPROPERTY(BlueprintAssignable, Category = "Messaging")
    FOnHybridComponentMessageReceived OnMessageReceived;

private:
    struct FActiveListener
    {
        FHybridMessageListenerHandle Handle;
        TWeakObjectPtr<UScriptStruct> PayloadType;
    };

    TMap<FGameplayTag, FActiveListener> ActiveListeners;

    const void* LastReceivedPayload = nullptr;
    TWeakObjectPtr<const UScriptStruct> LastReceivedPayloadType = nullptr;

    void HandleMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload);
};

#undef HYBRID_API
