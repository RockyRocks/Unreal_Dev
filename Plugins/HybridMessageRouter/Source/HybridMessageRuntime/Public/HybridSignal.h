// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HybridSlot.h"
#include "HybridSignalPolicies.h"
#include "HybridSignalArgDesc.h"
#include "Containers/Array.h"

/**
 * High-performance signal with intrusive-list slots and priority-based dispatch.
 *
 * This is the Tier 2 (direct signal-slot) component of the hybrid messaging system.
 * It combines System B's intrusive linked-list architecture with variadic templates
 * and UE-native thread safety policies.
 *
 * Template parameters:
 *   ArgDesc        - TSignalArgDesc<ArgTypes...> describing the signal signature
 *   PriorityLevels - Number of discrete priority bands (1 = no priorities, default)
 *   ThreadPolicy   - One of FGameThreadOnlyPolicy, FCriticalSectionPolicy, FReadWriteLockPolicy
 *
 * Performance characteristics:
 *   Connect/Disconnect: O(1) — intrusive list link/unlink
 *   Emit (no slots):    ~0 — inlined HasSlots() check
 *   Emit (N slots):     O(N) — snapshot into stack-local buffer, then virtual dispatch
 *   Memory per signal:  sizeof(Sentinel) * PriorityLevels + sizeof(ThreadPolicy)
 *
 * Usage:
 *   THybridSignal<TSignalArgDesc<int32, float>> MySignal;
 *   TMemberSlot<MyClass, TSignalArgDesc<int32, float>> MySlot(this, &MyClass::OnEvent);
 *   MySignal.Connect(MySlot, 0);
 *   MySignal.Emit(42, 3.14f);
 */
template <typename ArgDesc, int32 PriorityLevels = 1, typename ThreadPolicy = FGameThreadOnlyPolicy>
class THybridSignal
{
    static_assert(PriorityLevels >= 1, "Must have at least 1 priority level");

public:
    using SlotType = THybridSlot<ArgDesc>;
    static constexpr int32 NumPriorities = PriorityLevels;

    static constexpr int32 PriorityMaximum = 0;
    static constexpr int32 PriorityNormal = PriorityLevels / 2;
    static constexpr int32 PriorityMinimum = PriorityLevels - 1;

    THybridSignal()
    {
        for (int32 i = 0; i < PriorityLevels; ++i)
        {
            Sentinels[i].InitAsSentinel();
        }
    }

    ~THybridSignal()
    {
        DisconnectAll();
    }

    THybridSignal(const THybridSignal&) = delete;
    THybridSignal& operator=(const THybridSignal&) = delete;

    THybridSignal(THybridSignal&&) = delete;
    THybridSignal& operator=(THybridSignal&&) = delete;

    void Connect(SlotType& Slot, int32 Priority = PriorityNormal)
    {
        check(Priority >= 0 && Priority < PriorityLevels);

        if (Slot.IsConnected())
        {
            Slot.Disconnect();
        }

        typename ThreadPolicy::FScopedWriteLock Lock(Policy);

        Slot.OwnerSignal = this;
        Slot.SlotPriority = Priority;
        Slot.LinkBefore(&Sentinels[Priority]);
    }

    void Disconnect(SlotType& Slot)
    {
        if (Slot.OwnerSignal != this)
        {
            return;
        }

        typename ThreadPolicy::FScopedWriteLock Lock(Policy);

        Slot.Unlink();
        Slot.OwnerSignal = nullptr;
        Slot.SlotPriority = 0;
        bDirtyBuffer = true;
    }

    void DisconnectAll()
    {
        typename ThreadPolicy::FScopedWriteLock Lock(Policy);

        for (int32 i = 0; i < PriorityLevels; ++i)
        {
            SlotType* Current = Sentinels[i].Next;
            while (Current != &Sentinels[i])
            {
                SlotType* NextSlot = Current->Next;
                Current->Next = Current;
                Current->Prev = Current;
                Current->OwnerSignal = nullptr;
                Current->SlotPriority = 0;
                Current = NextSlot;
            }
            Sentinels[i].InitAsSentinel();
        }
        bDirtyBuffer = true;
    }

    bool HasSlots() const
    {
        for (int32 i = 0; i < PriorityLevels; ++i)
        {
            if (!Sentinels[i].IsEmpty())
            {
                return true;
            }
        }
        return false;
    }

    int32 GetSlotCount() const
    {
        typename ThreadPolicy::FScopedReadLock Lock(Policy);

        int32 Count = 0;
        for (int32 i = 0; i < PriorityLevels; ++i)
        {
            const SlotType* Current = Sentinels[i].Next;
            while (Current != &Sentinels[i])
            {
                ++Count;
                Current = Current->Next;
            }
        }
        return Count;
    }

private:
    template <typename... CallArgs>
    void EmitInternal(CallArgs&&... Args)
    {
        if (!HasSlots())
        {
            return;
        }

        typename ThreadPolicy::FScopedReadLock Lock(Policy);

        bDirtyBuffer = false;

        // Snapshot into inline buffer to allow safe mutation during dispatch
        static constexpr int32 InlineSlotCount = 64;
        TArray<SlotType*, TInlineAllocator<InlineSlotCount>> SlotBuffer;

        for (int32 i = 0; i < PriorityLevels; ++i)
        {
            SlotType* Current = Sentinels[i].Next;
            while (Current != &Sentinels[i])
            {
                SlotBuffer.Add(Current);
                Current = Current->Next;
            }
        }

        for (int32 Idx = 0; Idx < SlotBuffer.Num(); ++Idx)
        {
            SlotType* Slot = SlotBuffer[Idx];

            if (bDirtyBuffer)
            {
                if (Slot->OwnerSignal != this)
                {
                    continue;
                }
            }

            Slot->Invoke(Forward<CallArgs>(Args)...);
        }
    }

public:
    // Variadic Emit — the primary dispatch entry point
    template <typename... CallArgs>
    void Emit(CallArgs&&... Args)
    {
        EmitInternal(Forward<CallArgs>(Args)...);
    }

    // operator() convenience (System B compatibility)
    template <typename... CallArgs>
    void operator()(CallArgs&&... Args)
    {
        EmitInternal(Forward<CallArgs>(Args)...);
    }

private:
    SlotType Sentinels[PriorityLevels];
    mutable ThreadPolicy Policy;
    mutable bool bDirtyBuffer = false;
};
