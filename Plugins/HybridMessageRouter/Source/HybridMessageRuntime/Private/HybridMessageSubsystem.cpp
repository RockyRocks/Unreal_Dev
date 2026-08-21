// Copyright Epic Games, Inc. All Rights Reserved.

#include "HybridMessageSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/ScriptMacros.h"
#include "UObject/Stack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HybridMessageSubsystem)

DEFINE_LOG_CATEGORY(LogHybridMessage);

namespace UE::HybridMessage::Private
{
    static int32 ShouldLogMessages = 0;
    static FAutoConsoleVariableRef CVarShouldLogMessages(
        TEXT("HybridMessage.LogMessages"),
        ShouldLogMessages,
        TEXT("When non-zero, messages broadcast through the hybrid message subsystem are logged."));
}

// -------------------------------------------------------------------
// FHybridMessageListenerHandle
// -------------------------------------------------------------------

void FHybridMessageListenerHandle::Unregister()
{
    if (UHybridMessageSubsystem* StrongSubsystem = Subsystem.Get())
    {
        StrongSubsystem->UnregisterListener(*this);
        Subsystem.Reset();
        Channel = FGameplayTag();
        ID = 0;
    }
}

// -------------------------------------------------------------------
// UHybridMessageSubsystem
// -------------------------------------------------------------------

UHybridMessageSubsystem& UHybridMessageSubsystem::Get(const UObject* WorldContextObject)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
    check(World);
    UHybridMessageSubsystem* Router = UGameInstance::GetSubsystem<UHybridMessageSubsystem>(World->GetGameInstance());
    check(Router);
    return *Router;
}

bool UHybridMessageSubsystem::HasInstance(const UObject* WorldContextObject)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
    UHybridMessageSubsystem* Router = World != nullptr ? UGameInstance::GetSubsystem<UHybridMessageSubsystem>(World->GetGameInstance()) : nullptr;
    return Router != nullptr;
}

void UHybridMessageSubsystem::Deinitialize()
{
    {
        FRWScopeLock WriteLock(ListenerMapLock, SLT_Write);
        ListenerMap.Reset();
    }
    {
        FScopeLock CacheLock(&ParentTagCacheLock);
        ParentTagCache.Reset();
    }

    Super::Deinitialize();
}

// -------------------------------------------------------------------
// Parent Tag Cache
// -------------------------------------------------------------------

const TArray<FGameplayTag, TInlineAllocator<8>>& UHybridMessageSubsystem::GetOrBuildParentChainLocked(FGameplayTag Channel)
{
    // Caller must hold ParentTagCacheLock.
    if (const auto* Cached = ParentTagCache.Find(Channel))
    {
        return *Cached;
    }

    TArray<FGameplayTag, TInlineAllocator<8>>& Chain = ParentTagCache.Add(Channel);
    for (FGameplayTag Tag = Channel; Tag.IsValid(); Tag = Tag.RequestDirectParent())
    {
        Chain.Add(Tag);
    }
    return Chain;
}

// -------------------------------------------------------------------
// Broadcast
// -------------------------------------------------------------------

void UHybridMessageSubsystem::BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes)
{
    if (UE::HybridMessage::Private::ShouldLogMessages != 0)
    {
        FString* pContextString = nullptr;
#if WITH_EDITOR
        if (GIsEditor)
        {
            extern ENGINE_API FString GPlayInEditorContextString;
            pContextString = &GPlayInEditorContextString;
        }
#endif
        FString HumanReadableMessage;
        StructType->ExportText(HumanReadableMessage, MessageBytes, nullptr, nullptr, PPF_None, nullptr);
        UE_LOG(LogHybridMessage, Log, TEXT("BroadcastMessage(%s, %s, %s)"),
            pContextString ? **pContextString : *GetPathNameSafe(this),
            *Channel.ToString(),
            *HumanReadableMessage);
    }

    // Build the parent tag chain under its own lock (separate from listener map).
    // This avoids upgrading the listener read lock to write just for cache seeding.
    TArray<FGameplayTag, TInlineAllocator<8>> TagChainLocal;
    {
        FScopeLock CacheLock(&ParentTagCacheLock);
        TagChainLocal = GetOrBuildParentChainLocked(Channel);
    }

    // Phase 1: Snapshot all matching listeners under the read lock.
    // This keeps the lock window short and avoids deadlock if a callback
    // calls RegisterListener/UnregisterListener (which needs the write lock).
    struct FTagLevelSnapshot
    {
        FGameplayTag Tag;
        bool bIsInitialTag;
        TArray<FHybridMessageListenerData, TInlineAllocator<16>> Listeners;
    };

    TArray<FTagLevelSnapshot, TInlineAllocator<8>> AllSnapshots;

    {
        FRWScopeLock ReadLock(ListenerMapLock, SLT_ReadOnly);

        bool bOnInitialTag = true;
        for (const FGameplayTag& Tag : TagChainLocal)
        {
            const FChannelListenerList* pList = ListenerMap.Find(Tag);
            if (pList)
            {
                if (bOnInitialTag || pList->bHasPartialMatchListeners)
                {
                    FTagLevelSnapshot& Snap = AllSnapshots.AddDefaulted_GetRef();
                    Snap.Tag = Tag;
                    Snap.bIsInitialTag = bOnInitialTag;
                    Snap.Listeners = pList->Listeners;
                }
            }
            bOnInitialTag = false;
        }
    }
    // Read lock released — safe to invoke callbacks that may re-enter the subsystem.

    // Phase 2: Dispatch to snapshotted listeners outside the lock.
    for (const FTagLevelSnapshot& Snap : AllSnapshots)
    {
        bool bConsumed = false;
        for (const FHybridMessageListenerData& Listener : Snap.Listeners)
        {
            if (!Snap.bIsInitialTag && (Listener.MatchType != EHybridMessageMatch::PartialMatch))
            {
                continue;
            }

            if (Listener.bHadValidType && !Listener.ListenerStructType.IsValid())
            {
                UE_LOG(LogHybridMessage, Warning,
                    TEXT("Listener struct type has gone invalid on channel %s (HandleID=%d). Will be cleaned up."),
                    *Channel.ToString(), Listener.HandleID);
                continue;
            }

            if (Listener.bHadValidType && !StructType->IsChildOf(Listener.ListenerStructType.Get()))
            {
                UE_LOG(LogHybridMessage, Error,
                    TEXT("Struct type mismatch on channel %s (broadcast: %s, listener at %s expects: %s)"),
                    *Channel.ToString(),
                    *StructType->GetPathName(),
                    *Snap.Tag.ToString(),
                    *Listener.ListenerStructType->GetPathName());
                continue;
            }

            if (Listener.ContentFilter && !Listener.ContentFilter(StructType, MessageBytes))
            {
                continue;
            }

            EHybridMessageResult Result = Listener.ReceivedCallback(Channel, StructType, MessageBytes);
            if (Result == EHybridMessageResult::Consumed)
            {
                bConsumed = true;
                break;
            }
        }

        if (bConsumed)
        {
            return;
        }
    }
}

// -------------------------------------------------------------------
// Blueprint Broadcast (CustomThunk)
// -------------------------------------------------------------------

void UHybridMessageSubsystem::K2_BroadcastMessage(FGameplayTag Channel, const int32& Message)
{
    checkNoEntry();
}

DEFINE_FUNCTION(UHybridMessageSubsystem::execK2_BroadcastMessage)
{
    P_GET_STRUCT(FGameplayTag, Channel);

    Stack.MostRecentPropertyAddress = nullptr;
    Stack.StepCompiledIn<FStructProperty>(nullptr);
    void* MessagePtr = Stack.MostRecentPropertyAddress;
    FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

    P_FINISH;

    if (ensure((StructProp != nullptr) && (StructProp->Struct != nullptr) && (MessagePtr != nullptr)))
    {
        P_THIS->BroadcastMessageInternal(Channel, StructProp->Struct, MessagePtr);
    }
}

// -------------------------------------------------------------------
// Registration
// -------------------------------------------------------------------

FHybridMessageListenerHandle UHybridMessageSubsystem::RegisterListenerInternal(
    FGameplayTag Channel,
    TFunction<EHybridMessageResult(FGameplayTag, const UScriptStruct*, const void*)>&& Callback,
    const UScriptStruct* StructType,
    EHybridMessageMatch MatchType,
    EHybridMessagePriority Priority,
    TFunction<bool(const UScriptStruct*, const void*)>* ContentFilter)
{
    FRWScopeLock WriteLock(ListenerMapLock, SLT_Write);

    FChannelListenerList& List = ListenerMap.FindOrAdd(Channel);

    FHybridMessageListenerData& Entry = List.Listeners.AddDefaulted_GetRef();
    Entry.ReceivedCallback = MoveTemp(Callback);
    Entry.ListenerStructType = StructType;
    Entry.bHadValidType = (StructType != nullptr);
    Entry.HandleID = ++List.NextHandleID;
    Entry.MatchType = MatchType;
    Entry.Priority = Priority;

    if (ContentFilter && *ContentFilter)
    {
        Entry.ContentFilter = MoveTemp(*ContentFilter);
    }

    if (MatchType == EHybridMessageMatch::PartialMatch)
    {
        List.bHasPartialMatchListeners = true;
    }

    const int32 HandleID = Entry.HandleID;
    const uint8 PriorityValue = static_cast<uint8>(Priority);

    // Maintain priority ordering via insertion sort.
    // Registration is infrequent relative to broadcast, so this is acceptable.
    // Note: Entry ref is invalidated by Swap, so we use index-based access.
    int32 CurrentIndex = List.Listeners.Num() - 1;
    for (int32 i = CurrentIndex - 1; i >= 0; --i)
    {
        if (static_cast<uint8>(List.Listeners[i].Priority) <= PriorityValue)
        {
            break;
        }
        List.Listeners.Swap(i, i + 1);
        CurrentIndex = i;
    }

    return FHybridMessageListenerHandle(this, Channel, HandleID);
}

// -------------------------------------------------------------------
// Unregistration
// -------------------------------------------------------------------

void UHybridMessageSubsystem::UnregisterListener(FHybridMessageListenerHandle Handle)
{
    if (Handle.IsValid())
    {
        check(Handle.Subsystem == this);
        UnregisterListenerInternal(Handle.Channel, Handle.ID);
    }
    else
    {
        UE_LOG(LogHybridMessage, Warning, TEXT("Trying to unregister an invalid Handle."));
    }
}

void UHybridMessageSubsystem::UnregisterListenerInternal(FGameplayTag Channel, int32 HandleID)
{
    FRWScopeLock WriteLock(ListenerMapLock, SLT_Write);

    FChannelListenerList* pList = ListenerMap.Find(Channel);
    if (!pList)
    {
        return;
    }

    const int32 MatchIndex = pList->Listeners.IndexOfByPredicate(
        [HandleID](const FHybridMessageListenerData& Other) { return Other.HandleID == HandleID; });

    if (MatchIndex != INDEX_NONE)
    {
        const bool bWasPartial = (pList->Listeners[MatchIndex].MatchType == EHybridMessageMatch::PartialMatch);

        pList->Listeners.RemoveAt(MatchIndex);

        if (bWasPartial)
        {
            pList->RefreshPartialMatchFlag();
        }
    }

    if (pList->Listeners.Num() == 0)
    {
        ListenerMap.Remove(Channel);
    }
}
