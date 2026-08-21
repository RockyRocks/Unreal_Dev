// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HybridMessageTypes.h"
#include "GameplayTagContainer.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/WeakObjectPtr.h"

#include "HybridMessageSubsystem.generated.h"

#define HYBRID_API HYBRIDMESSAGERUNTIME_API

class UHybridMessageSubsystem;
class UAsyncAction_ListenForHybridMessage;
class UHybridSignalComponent;
struct FFrame;

HYBRIDMESSAGERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogHybridMessage, Log, All);

/**
 * Tier 1 centralized message bus.
 *
 * Provides tag-based hierarchical pub/sub with:
 *  - Priority-ordered listeners (First → Last)
 *  - Type covariance (IsChildOf)
 *  - Message consumption (Consumed stops propagation)
 *  - Content-based filtering
 *  - RW-lock thread safety
 *  - Zero-heap-alloc dispatch for typical listener counts (TInlineAllocator<16>)
 *  - Parent tag cache to avoid RequestDirectParent() walk per broadcast
 *  - Blueprint support via CustomThunk
 *
 * Note that listener call order within the same priority level is NOT
 * guaranteed and may change when listeners are added or removed.
 */
UCLASS(MinimalAPI)
class UHybridMessageSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    friend UAsyncAction_ListenForHybridMessage;
    friend class UHybridSignalComponent;

public:
    static HYBRID_API UHybridMessageSubsystem& Get(const UObject* WorldContextObject);
    static HYBRID_API bool HasInstance(const UObject* WorldContextObject);

    HYBRID_API virtual void Deinitialize() override;

    // ---- Tier 1 Broadcast API ----

    /**
     * Broadcast a message on the specified channel.
     * All registered listeners whose channel and type match will be notified
     * in priority order. A listener returning Consumed stops further propagation.
     */
    template <typename FMessageStructType>
    void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message)
    {
        const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
        BroadcastMessageInternal(Channel, StructType, &Message);
    }

    // ---- Tier 1 Listener Registration API ----

    /**
     * Register a lambda / TFunction callback.
     */
    template <typename FMessageStructType>
    FHybridMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback,
        EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch,
        EHybridMessagePriority Priority = EHybridMessagePriority::Normal)
    {
        auto ThunkCallback = [InnerCallback = MoveTemp(Callback)](FGameplayTag ActualTag, const UScriptStruct* SenderStructType, const void* SenderPayload) -> EHybridMessageResult
        {
            InnerCallback(ActualTag, *reinterpret_cast<const FMessageStructType*>(SenderPayload));
            return EHybridMessageResult::Handled;
        };

        const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
        return RegisterListenerInternal(Channel, MoveTemp(ThunkCallback), StructType, MatchType, Priority, nullptr);
    }

    /**
     * Register a weak-object member function callback.
     */
    template <typename FMessageStructType, typename TOwner = UObject>
    FHybridMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* Object,
        void(TOwner::* Function)(FGameplayTag, const FMessageStructType&),
        EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch,
        EHybridMessagePriority Priority = EHybridMessagePriority::Normal)
    {
        TWeakObjectPtr<TOwner> WeakObject(Object);
        return RegisterListener<FMessageStructType>(Channel,
            [WeakObject, Function](FGameplayTag ActualChannel, const FMessageStructType& Payload)
            {
                if (TOwner* StrongObject = WeakObject.Get())
                {
                    (StrongObject->*Function)(ActualChannel, Payload);
                }
            },
            MatchType, Priority);
    }

    /**
     * Register with a result-returning callback that can consume messages.
     */
    template <typename FMessageStructType>
    FHybridMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<EHybridMessageResult(FGameplayTag, const FMessageStructType&)>&& Callback,
        EHybridMessageMatch MatchType = EHybridMessageMatch::ExactMatch,
        EHybridMessagePriority Priority = EHybridMessagePriority::Normal)
    {
        auto ThunkCallback = [InnerCallback = MoveTemp(Callback)](FGameplayTag ActualTag, const UScriptStruct* SenderStructType, const void* SenderPayload) -> EHybridMessageResult
        {
            return InnerCallback(ActualTag, *reinterpret_cast<const FMessageStructType*>(SenderPayload));
        };

        const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
        return RegisterListenerInternal(Channel, MoveTemp(ThunkCallback), StructType, MatchType, Priority, nullptr);
    }

    /**
     * Register with advanced parameters struct (match type, priority, content filter).
     */
    template <typename FMessageStructType>
    FHybridMessageListenerHandle RegisterListener(FGameplayTag Channel, FHybridMessageListenerParams<FMessageStructType>& Params)
    {
        FHybridMessageListenerHandle Handle;

        if (!Params.OnMessageReceivedCallback)
        {
            return Handle;
        }

        auto ThunkCallback = [InnerCallback = Params.OnMessageReceivedCallback](FGameplayTag ActualTag, const UScriptStruct* SenderStructType, const void* SenderPayload) -> EHybridMessageResult
        {
            InnerCallback(ActualTag, *reinterpret_cast<const FMessageStructType*>(SenderPayload));
            return EHybridMessageResult::Handled;
        };

        TFunction<bool(const UScriptStruct*, const void*)> ContentFilterThunk;
        if (Params.ContentFilter)
        {
            ContentFilterThunk = [InnerFilter = Params.ContentFilter](const UScriptStruct* StructType, const void* Payload) -> bool
            {
                return InnerFilter(*reinterpret_cast<const FMessageStructType*>(Payload));
            };
        }

        const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
        Handle = RegisterListenerInternal(Channel, MoveTemp(ThunkCallback), StructType, Params.MatchType, Params.Priority, ContentFilterThunk ? &ContentFilterThunk : nullptr);

        return Handle;
    }

    HYBRID_API void UnregisterListener(FHybridMessageListenerHandle Handle);

protected:
    UFUNCTION(BlueprintCallable, CustomThunk, Category=Messaging, meta=(CustomStructureParam="Message", AllowAbstract="false", DisplayName="Broadcast Hybrid Message"))
    HYBRID_API void K2_BroadcastMessage(FGameplayTag Channel, const int32& Message);

    DECLARE_FUNCTION(execK2_BroadcastMessage);

private:
    HYBRID_API void BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes);

    HYBRID_API FHybridMessageListenerHandle RegisterListenerInternal(
        FGameplayTag Channel,
        TFunction<EHybridMessageResult(FGameplayTag, const UScriptStruct*, const void*)>&& Callback,
        const UScriptStruct* StructType,
        EHybridMessageMatch MatchType,
        EHybridMessagePriority Priority,
        TFunction<bool(const UScriptStruct*, const void*)>* ContentFilter);

    HYBRID_API void UnregisterListenerInternal(FGameplayTag Channel, int32 HandleID);

private:
    struct FChannelListenerList
    {
        TArray<FHybridMessageListenerData> Listeners;
        int32 NextHandleID = 0;
        bool bHasPartialMatchListeners = false;

        void RefreshPartialMatchFlag()
        {
            bHasPartialMatchListeners = false;
            for (const FHybridMessageListenerData& L : Listeners)
            {
                if (L.MatchType == EHybridMessageMatch::PartialMatch)
                {
                    bHasPartialMatchListeners = true;
                    return;
                }
            }
        }
    };

    TMap<FGameplayTag, FChannelListenerList> ListenerMap;

    // Cache of parent tag chains: Tag -> [Tag, Parent, Grandparent, ...]
    // GameplayTag hierarchies are immutable at runtime, so this cache never goes stale.
    // Guarded by its own lock to avoid promoting to write during broadcast.
    TMap<FGameplayTag, TArray<FGameplayTag, TInlineAllocator<8>>> ParentTagCache;
    mutable FCriticalSection ParentTagCacheLock;

    mutable FRWLock ListenerMapLock;

    // Must be called with ParentTagCacheLock held.
    const TArray<FGameplayTag, TInlineAllocator<8>>& GetOrBuildParentChainLocked(FGameplayTag Channel);
};

#undef HYBRID_API
