// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HybridSignal.h"
#include "HybridMessageSubsystem.h"
#include "Engine/World.h"

/**
 * A signal that emits on both Tier 2 (direct slots) and Tier 1 (central hub).
 *
 * Use when you want the performance of direct signal-slot connections for
 * closely-coupled listeners, but also want anonymous/tag-based listeners
 * to receive the message through the central subsystem.
 *
 * Template parameters match THybridSignal, except the message type must be
 * a USTRUCT() that can travel through the Tier 1 hub.
 *
 * Usage:
 *   // Declare with your USTRUCT payload
 *   TBridgedSignal<FMyDamageEvent> OnDamage;
 *
 *   // Direct Tier 2 listeners connect as normal
 *   TMemberSlot<AMyActor, TSignalArgDesc<FGameplayTag, const FMyDamageEvent&>> MySlot;
 *   OnDamage.Connect(MySlot);
 *
 *   // Emit to both tiers
 *   OnDamage.EmitAndBroadcast(this, GameplayTag, DamageEvent);
 */
template <
    typename FMessageStructType,
    int32 PriorityLevels = 1,
    typename ThreadPolicy = FGameThreadOnlyPolicy
>
class TBridgedSignal
{
public:
    using ArgDesc = TSignalArgDesc<FGameplayTag, const FMessageStructType&>;
    using SignalType = THybridSignal<ArgDesc, PriorityLevels, ThreadPolicy>;
    using SlotType = typename SignalType::SlotType;

    void Connect(SlotType& Slot, int32 Priority = SignalType::PriorityNormal)
    {
        DirectSignal.Connect(Slot, Priority);
    }

    void Disconnect(SlotType& Slot)
    {
        DirectSignal.Disconnect(Slot);
    }

    void DisconnectAll()
    {
        DirectSignal.DisconnectAll();
    }

    bool HasSlots() const { return DirectSignal.HasSlots(); }

    /**
     * Emit to direct (Tier 2) slots only.
     * Use when you do not need the Tier 1 hub to relay the message.
     */
    void Emit(FGameplayTag Channel, const FMessageStructType& Message)
    {
        DirectSignal.Emit(Channel, Message);
    }

    /**
     * Emit to Tier 2 direct slots first, then broadcast through the Tier 1 hub.
     * Tier 2 dispatch happens synchronously before Tier 1 dispatch.
     *
     * @param WorldContext   Any UObject with a valid world (used to find the hub subsystem)
     * @param Channel        The gameplay tag channel
     * @param Message        The USTRUCT payload
     */
    void EmitAndBroadcast(const UObject* WorldContext, FGameplayTag Channel, const FMessageStructType& Message)
    {
        DirectSignal.Emit(Channel, Message);

        if (UHybridMessageSubsystem::HasInstance(WorldContext))
        {
            UHybridMessageSubsystem::Get(WorldContext).BroadcastMessage<FMessageStructType>(Channel, Message);
        }
    }

private:
    SignalType DirectSignal;
};
