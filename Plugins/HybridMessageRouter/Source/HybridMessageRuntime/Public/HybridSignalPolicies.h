// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

/**
 * Thread safety policies for THybridSignal.
 *
 * FGameThreadOnlyPolicy      - Zero overhead in shipping. Debug assertion that we are on game thread.
 * FCriticalSectionPolicy     - Per-signal critical section. Use when signals are emitted from multiple threads.
 * FReadWriteLockPolicy       - Reader-writer lock. Optimal when broadcasts vastly outnumber registration changes.
 */

struct FGameThreadOnlyPolicy
{
    struct FScopedLock
    {
        FORCEINLINE FScopedLock(const FGameThreadOnlyPolicy&)
        {
            checkSlow(IsInGameThread());
        }
    };

    using FScopedReadLock = FScopedLock;
    using FScopedWriteLock = FScopedLock;
};

struct FCriticalSectionPolicy
{
    mutable FCriticalSection CritSection;

    struct FScopedLock
    {
        FCriticalSection& CS;

        FORCEINLINE explicit FScopedLock(const FCriticalSectionPolicy& InPolicy)
            : CS(const_cast<FCriticalSection&>(InPolicy.CritSection))
        {
            CS.Lock();
        }

        FORCEINLINE ~FScopedLock()
        {
            CS.Unlock();
        }

        FScopedLock(const FScopedLock&) = delete;
        FScopedLock& operator=(const FScopedLock&) = delete;
    };

    using FScopedReadLock = FScopedLock;
    using FScopedWriteLock = FScopedLock;
};

struct FReadWriteLockPolicy
{
    mutable FRWLock Lock;

    struct FScopedReadLock
    {
        FRWLock& LockRef;

        FORCEINLINE explicit FScopedReadLock(const FReadWriteLockPolicy& InPolicy)
            : LockRef(const_cast<FRWLock&>(InPolicy.Lock))
        {
            LockRef.ReadLock();
        }

        FORCEINLINE ~FScopedReadLock()
        {
            LockRef.ReadUnlock();
        }

        FScopedReadLock(const FScopedReadLock&) = delete;
        FScopedReadLock& operator=(const FScopedReadLock&) = delete;
    };

    struct FScopedWriteLock
    {
        FRWLock& LockRef;

        FORCEINLINE explicit FScopedWriteLock(const FReadWriteLockPolicy& InPolicy)
            : LockRef(const_cast<FRWLock&>(InPolicy.Lock))
        {
            LockRef.WriteLock();
        }

        FORCEINLINE ~FScopedWriteLock()
        {
            LockRef.WriteUnlock();
        }

        FScopedWriteLock(const FScopedWriteLock&) = delete;
        FScopedWriteLock& operator=(const FScopedWriteLock&) = delete;
    };

    using FScopedLock = FScopedWriteLock;
};
