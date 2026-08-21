// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HybridSignalArgDesc.h"
#include "HAL/Platform.h"
#include "Templates/UnrealTemplate.h"

template <typename ArgDesc, int32 PriorityLevels, typename ThreadPolicy>
class THybridSignal;

/**
 * Intrusive doubly-linked list node that connects to a THybridSignal.
 *
 * Slots are non-copyable. Destroying a connected slot auto-disconnects it
 * from its owning signal (deterministic lifecycle from System B).
 *
 * Derived classes (TMemberSlot, TLambdaSlot, TStaticSlot) override Invoke()
 * to dispatch to the actual callback.
 */
template <typename ArgDesc>
class THybridSlot : public ArgDesc::ISlotCallable
{
public:
    THybridSlot()
        : Next(this)
        , Prev(this)
        , OwnerSignal(nullptr)
        , SlotPriority(0)
    {
    }

    virtual ~THybridSlot()
    {
        Disconnect();
    }

    THybridSlot(const THybridSlot&) = delete;
    THybridSlot& operator=(const THybridSlot&) = delete;

    THybridSlot(THybridSlot&& Other) noexcept
        : Next(this)
        , Prev(this)
        , OwnerSignal(nullptr)
        , SlotPriority(0)
    {
        TakeConnectionFrom(Other);
    }

    THybridSlot& operator=(THybridSlot&& Other) noexcept
    {
        if (this != &Other)
        {
            Disconnect();
            TakeConnectionFrom(Other);
        }
        return *this;
    }

    bool IsConnected() const { return OwnerSignal != nullptr; }
    int32 GetPriority() const { return SlotPriority; }

    void Disconnect()
    {
        if (OwnerSignal != nullptr)
        {
            Unlink();
            OwnerSignal = nullptr;
            SlotPriority = 0;
        }
    }

private:
    template <typename, int32, typename>
    friend class THybridSignal;

    THybridSlot* Next;
    THybridSlot* Prev;
    void* OwnerSignal;
    int32 SlotPriority;

    void InitAsSentinel()
    {
        Next = this;
        Prev = this;
    }

    bool IsSentinel() const
    {
        return (Next == this) && (Prev == this) && (OwnerSignal == nullptr);
    }

    bool IsEmpty() const
    {
        return Next == this;
    }

    void LinkAfter(THybridSlot* Node)
    {
        this->Prev = Node;
        this->Next = Node->Next;
        Node->Next->Prev = this;
        Node->Next = this;
    }

    void LinkBefore(THybridSlot* Node)
    {
        this->Next = Node;
        this->Prev = Node->Prev;
        Node->Prev->Next = this;
        Node->Prev = this;
    }

    void Unlink()
    {
        Prev->Next = Next;
        Next->Prev = Prev;
        Next = this;
        Prev = this;
    }

    void TakeConnectionFrom(THybridSlot& Other)
    {
        if (Other.IsConnected())
        {
            OwnerSignal = Other.OwnerSignal;
            SlotPriority = Other.SlotPriority;

            Next = Other.Next;
            Prev = Other.Prev;
            Next->Prev = this;
            Prev->Next = this;

            Other.Next = &Other;
            Other.Prev = &Other;
            Other.OwnerSignal = nullptr;
            Other.SlotPriority = 0;
        }
    }
};

/**
 * Slot bound to a member function pointer on a specific object.
 * The object pointer is stored raw — the caller is responsible for ensuring
 * the object outlives the slot (or using the weak-pointer variant below).
 */
template <typename TObject, typename ArgDesc>
class TMemberSlot;

template <typename TObject, typename... ArgTypes>
class TMemberSlot<TObject, TSignalArgDesc<ArgTypes...>> : public THybridSlot<TSignalArgDesc<ArgTypes...>>
{
public:
    using FuncPtr = void (TObject::*)(ArgTypes...);

    TMemberSlot() : Object(nullptr), Function(nullptr) {}

    TMemberSlot(TObject* InObject, FuncPtr InFunction)
        : Object(InObject)
        , Function(InFunction)
    {
    }

    void Bind(TObject* InObject, FuncPtr InFunction)
    {
        Object = InObject;
        Function = InFunction;
    }

    virtual void Invoke(ArgTypes... Args) override
    {
        if (Object && Function)
        {
            (Object->*Function)(Args...);
        }
    }

private:
    TObject* Object;
    FuncPtr Function;
};

/**
 * Slot bound to a static / free function.
 */
template <typename ArgDesc>
class TStaticSlot;

template <typename... ArgTypes>
class TStaticSlot<TSignalArgDesc<ArgTypes...>> : public THybridSlot<TSignalArgDesc<ArgTypes...>>
{
public:
    using FuncPtr = void (*)(ArgTypes...);

    TStaticSlot() : Function(nullptr) {}

    explicit TStaticSlot(FuncPtr InFunction)
        : Function(InFunction)
    {
    }

    void Bind(FuncPtr InFunction)
    {
        Function = InFunction;
    }

    virtual void Invoke(ArgTypes... Args) override
    {
        if (Function)
        {
            Function(Args...);
        }
    }

private:
    FuncPtr Function;
};

/**
 * Slot bound to a TFunction<> (lambda, functor, etc.).
 */
template <typename ArgDesc>
class TLambdaSlot;

template <typename... ArgTypes>
class TLambdaSlot<TSignalArgDesc<ArgTypes...>> : public THybridSlot<TSignalArgDesc<ArgTypes...>>
{
public:
    TLambdaSlot() = default;

    explicit TLambdaSlot(TFunction<void(ArgTypes...)>&& InCallback)
        : Callback(MoveTemp(InCallback))
    {
    }

    void Bind(TFunction<void(ArgTypes...)>&& InCallback)
    {
        Callback = MoveTemp(InCallback);
    }

    virtual void Invoke(ArgTypes... Args) override
    {
        if (Callback)
        {
            Callback(Args...);
        }
    }

private:
    TFunction<void(ArgTypes...)> Callback;
};
