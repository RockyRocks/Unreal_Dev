// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

/**
 * Variadic argument descriptor for THybridSignal / THybridSlot.
 *
 * Replaces System B's macro-generated DescArg0..DescArg8 with a single
 * variadic template. No argument count limit.
 *
 * Usage:
 *   using MyDesc = TSignalArgDesc<int32, float, const FVector&>;
 *   THybridSignal<MyDesc> MySignal;
 */
template <typename... ArgTypes>
struct TSignalArgDesc
{
    static constexpr int32 ArgCount = sizeof...(ArgTypes);

    class ISlotCallable
    {
    public:
        virtual ~ISlotCallable() = default;
        virtual void Invoke(ArgTypes... Args) = 0;
    };
};
