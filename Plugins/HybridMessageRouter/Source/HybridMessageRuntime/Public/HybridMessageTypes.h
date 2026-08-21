// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtr.h"

#include "HybridMessageTypes.generated.h"

class UHybridMessageSubsystem;

UENUM(BlueprintType)
enum class EHybridMessageMatch : uint8
{
    // Listener receives only messages with an exactly matching channel tag.
    ExactMatch,

    // Listener receives messages rooted at its registered channel.
    // Registering for "A.B" also receives broadcasts of "A.B.C", "A.B.C.D", etc.
    PartialMatch
};

UENUM(BlueprintType)
enum class EHybridMessagePriority : uint8
{
    First   = 0    UMETA(DisplayName = "First (Interceptors)"),
    High    = 64   UMETA(DisplayName = "High"),
    Normal  = 128  UMETA(DisplayName = "Normal"),
    Low     = 192  UMETA(DisplayName = "Low"),
    Last    = 255  UMETA(DisplayName = "Last (Logging)")
};

UENUM(BlueprintType)
enum class EHybridMessageResult : uint8
{
    // Message processed. Continue propagation to remaining listeners.
    Handled,

    // Message processed. Stop propagation — no further listeners will receive it.
    Consumed,

    // Message was not relevant to this listener. Continue propagation.
    Skipped
};

/**
 * Opaque handle returned by RegisterListener. Call Unregister() to remove
 * the listener, or let the handle go out of scope (it does NOT auto-unregister).
 */
USTRUCT(BlueprintType)
struct HYBRIDMESSAGERUNTIME_API FHybridMessageListenerHandle
{
    GENERATED_BODY()

    FHybridMessageListenerHandle() {}

    void Unregister();

    bool IsValid() const { return ID != 0; }

private:
    friend UHybridMessageSubsystem;

    UPROPERTY(Transient)
    TWeakObjectPtr<UHybridMessageSubsystem> Subsystem;

    UPROPERTY(Transient)
    FGameplayTag Channel;

    UPROPERTY(Transient)
    int32 ID = 0;

    FHybridMessageListenerHandle(UHybridMessageSubsystem* InSubsystem, FGameplayTag InChannel, int32 InID)
        : Subsystem(InSubsystem), Channel(InChannel), ID(InID)
    {
    }
};

/**
 * Internal storage for a single registered Tier 1 listener.
 */
USTRUCT()
struct FHybridMessageListenerData
{
    GENERATED_BODY()

    // Callback signature: returns EHybridMessageResult so listeners can consume messages.
    // The raw void* payload + UScriptStruct* are passed for type-erased dispatch.
    TFunction<EHybridMessageResult(FGameplayTag, const UScriptStruct*, const void*)> ReceivedCallback;

    int32 HandleID = 0;
    EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch;
    EHybridMessagePriority Priority = EHybridMessagePriority::Normal;

    TWeakObjectPtr<const UScriptStruct> ListenerStructType = nullptr;
    bool bHadValidType = false;

    // Optional content-based filter predicate. If bound, the listener's callback
    // is only invoked when this returns true.
    TFunction<bool(const UScriptStruct*, const void*)> ContentFilter;
};

/**
 * Advanced registration parameters for Tier 1 listeners.
 */
template <typename FMessageStructType>
struct FHybridMessageListenerParams
{
    EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch;
    EHybridMessagePriority Priority = EHybridMessagePriority::Normal;

    TFunction<void(FGameplayTag, const FMessageStructType&)> OnMessageReceivedCallback;

    // Optional content filter. Return true to receive the message, false to skip.
    TFunction<bool(const FMessageStructType&)> ContentFilter;

    template <typename TOwner = UObject>
    void SetMessageReceivedCallback(TOwner* Object, void(TOwner::* Function)(FGameplayTag, const FMessageStructType&))
    {
        TWeakObjectPtr<TOwner> WeakObject(Object);
        OnMessageReceivedCallback = [WeakObject, Function](FGameplayTag Channel, const FMessageStructType& Payload)
        {
            if (TOwner* StrongObject = WeakObject.Get())
            {
                (StrongObject->*Function)(Channel, Payload);
            }
        };
    }
};
