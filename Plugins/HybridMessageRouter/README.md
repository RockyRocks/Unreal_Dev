# HybridMessageRouter

A two-tier messaging plugin for Unreal Engine that combines centralized tag-based pub/sub with high-performance decentralized signal-slot dispatch.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Installation](#installation)
3. [Tier 1: Tagged Message Bus](#tier-1-tagged-message-bus)
4. [Tier 2: Direct Signal-Slot](#tier-2-direct-signal-slot)
5. [Bridge: Dual-Tier Emission](#bridge-dual-tier-emission)
6. [Blueprint Integration](#blueprint-integration)
7. [Thread Safety](#thread-safety)
8. [Performance Characteristics](#performance-characteristics)
9. [Best Practices](#best-practices)
10. [Anti-Patterns](#anti-patterns)
11. [API Reference](#api-reference)
12. [Migration from GameplayMessageRouter](#migration-from-gameplaymessagerouter)

---

## Architecture Overview

HybridMessageRouter provides two complementary messaging tiers and a bridge between them:

```
+------------------------------------------------------+
|                  Your Game Code                       |
+------------------------------------------------------+
       |                    |                  |
       v                    v                  v
+--------------+   +-----------------+   +------------+
| Tier 1       |   | Bridge          |   | Tier 2     |
| Message Bus  |   | TBridgedSignal  |   | Signal-Slot|
| (Tag-based)  |   | (Both tiers)    |   | (Direct)   |
+--------------+   +-----------------+   +------------+
| UHybridMsg   |   | Emits to Tier 2 |   | THybridSig |
| Subsystem    |   | then broadcasts |   | THybridSlot|
|              |   | to Tier 1       |   |            |
| FGameplayTag |   |                 |   | Variadic   |
| Hierarchical |   |                 |   | Template   |
| Priority     |   |                 |   | Priority   |
| Consumable   |   |                 |   | O(1) ops   |
| Blueprint    |   |                 |   | Intrusive  |
+--------------+   +-----------------+   +------------+
```

**Tier 1 — Tagged Message Bus** is a centralized `UGameInstanceSubsystem` where any object can broadcast or listen using `FGameplayTag` channels. Supports hierarchical matching, type covariance, priority ordering, message consumption, content filtering, and full Blueprint integration. Choose Tier 1 when communicating between loosely coupled systems.

**Tier 2 — Direct Signal-Slot** is a decentralized template system where each object owns its own `THybridSignal` members. Slots connect directly to signals with O(1) intrusive list operations. Zero heap allocation for typical slot counts. Choose Tier 2 for high-frequency, performance-critical communication between closely related objects.

**Bridge — TBridgedSignal** emits to Tier 2 direct slots first, then broadcasts through Tier 1. Use it when a signal needs both the performance of direct connections and the discoverability of the central hub.

### When to Use Which Tier

| Scenario | Tier |
|---|---|
| Achievement system listens for combat events without importing combat headers | **Tier 1** |
| UI listens for inventory changes across all item categories | **Tier 1** (PartialMatch) |
| Physics collision callback on owning component | **Tier 2** |
| Animation state machine notifies owning character | **Tier 2** |
| Damage pipeline where both direct subscribers and global analytics need the event | **Bridge** |
| Blueprint-driven gameplay events | **Tier 1** |
| High-frequency per-frame sensor updates | **Tier 2** |

---

## Installation

1. Copy the `HybridMessageRouter` folder into your project's `Plugins/` directory.
2. Add `"HybridMessageRuntime"` to your module's `Build.cs` dependencies:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "HybridMessageRuntime"
});
```

3. Regenerate project files and build.

---

## Tier 1: Tagged Message Bus

### Defining a Message Struct

Messages are standard `USTRUCT` types:

```cpp
USTRUCT(BlueprintType)
struct FDamageMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Amount = 0.f;

    UPROPERTY(BlueprintReadWrite)
    AActor* Instigator = nullptr;

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag DamageType;
};
```

### Broadcasting

```cpp
FDamageMessage Msg;
Msg.Amount = 50.f;
Msg.Instigator = this;
Msg.DamageType = TAG_Damage_Fire;

UHybridMessageSubsystem& Router = UHybridMessageSubsystem::Get(this);
Router.BroadcastMessage(TAG_Combat_Damage, Msg);
```

### Listening — Lambda

```cpp
UHybridMessageSubsystem& Router = UHybridMessageSubsystem::Get(this);

Handle = Router.RegisterListener<FDamageMessage>(
    TAG_Combat_Damage,
    [this](FGameplayTag Channel, const FDamageMessage& Msg)
    {
        ApplyDamage(Msg.Amount, Msg.Instigator);
    });
```

### Listening — Member Function (Weak Object)

```cpp
Handle = Router.RegisterListener<FDamageMessage>(
    TAG_Combat_Damage,
    this,                           // stored as TWeakObjectPtr
    &AMyCharacter::OnDamageReceived);
```

The callback is automatically skipped if the object is garbage collected — no dangling pointer risk.

### Listening — With Priority and Match Type

```cpp
Handle = Router.RegisterListener<FDamageMessage>(
    TAG_Combat,                                      // parent tag
    [this](FGameplayTag Channel, const FDamageMessage& Msg)
    {
        LogCombatEvent(Channel, Msg);
    },
    EHybridMessageMatch::PartialMatch,               // receives TAG_Combat_Damage, TAG_Combat_Heal, etc.
    EHybridMessagePriority::Last);                   // runs after all Normal-priority listeners
```

### Listening — Message Consumption

A result-returning callback can stop propagation:

```cpp
Handle = Router.RegisterListener<FDamageMessage>(
    TAG_Combat_Damage,
    [this](FGameplayTag Channel, const FDamageMessage& Msg) -> EHybridMessageResult
    {
        if (bIsInvulnerable)
        {
            return EHybridMessageResult::Consumed;   // no further listeners fire
        }
        ApplyDamage(Msg.Amount, Msg.Instigator);
        return EHybridMessageResult::Handled;        // continue to next listener
    },
    EHybridMessageMatch::ExactMatch,
    EHybridMessagePriority::First);                  // interceptor: runs before everything
```

### Listening — Content Filter

Filter messages before your callback fires, avoiding unnecessary work:

```cpp
FHybridMessageListenerParams<FDamageMessage> Params;
Params.MatchType = EHybridMessageMatch::ExactMatch;
Params.Priority = EHybridMessagePriority::Normal;
Params.ContentFilter = [](const FDamageMessage& Msg)
{
    return Msg.Amount > 100.f;   // only care about big hits
};
Params.SetMessageReceivedCallback(this, &AMyCharacter::OnBigHit);

Handle = Router.RegisterListener(TAG_Combat_Damage, Params);
```

### Unregistering

```cpp
// Option A: Explicit
Router.UnregisterListener(Handle);

// Option B: Via handle
Handle.Unregister();

// Both are safe to call multiple times
```

### Hierarchical Channel Matching

`PartialMatch` listeners on a parent tag receive messages broadcast on any child tag:

```
Listener on TAG_Combat (PartialMatch) receives:
  ├── TAG_Combat                (exact match)
  ├── TAG_Combat_Damage         (child)
  ├── TAG_Combat_Damage_Fire    (grandchild)
  └── TAG_Combat_Heal           (child)
```

Parent tag chains are cached after the first broadcast per unique channel — subsequent broadcasts on the same channel skip the `RequestDirectParent()` walk entirely.

### Priority Order

Listeners execute in priority order, lowest numeric value first:

| Priority | Value | Typical Use |
|---|---|---|
| `First` | 0 | Interceptors, validation, input modals |
| `High` | 64 | Important game logic |
| `Normal` | 128 | Default — standard gameplay responses |
| `Low` | 192 | Secondary effects, cosmetics |
| `Last` | 255 | Logging, analytics, debug tools |

Listeners within the same priority level have **no guaranteed order**.

### Type Covariance

A listener registered for a base struct type automatically receives messages of derived struct types:

```cpp
// Base
USTRUCT(BlueprintType)
struct FCombatMessage { GENERATED_BODY() ... };

// Derived
USTRUCT(BlueprintType)
struct FDamageMessage : public FCombatMessage { GENERATED_BODY() ... };

// This listener receives both FCombatMessage and FDamageMessage broadcasts
Handle = Router.RegisterListener<FCombatMessage>(TAG_Combat_Damage, ...);
```

---

## Tier 2: Direct Signal-Slot

### Declaring a Signal

```cpp
// In your class header
using FOnDamageDesc = TSignalArgDesc<float /*Amount*/, AActor* /*Instigator*/>;

THybridSignal<FOnDamageDesc> OnDamage;
```

### Declaring Slots

```cpp
// Member function slot
TMemberSlot<AMyCharacter, FOnDamageDesc> DamageSlot;

// Lambda slot
TLambdaSlot<FOnDamageDesc> LoggingSlot;

// Static/free function slot
TStaticSlot<FOnDamageDesc> GlobalSlot;
```

### Connecting

```cpp
// Bind and connect a member slot
DamageSlot.Bind(this, &AMyCharacter::HandleDamage);
SomeEmitter->OnDamage.Connect(DamageSlot);

// Connect with priority (requires multi-priority signal)
SomeEmitter->OnDamage.Connect(DamageSlot, THybridSignal<FOnDamageDesc, 4>::PriorityMaximum);

// Lambda slot
LoggingSlot.Bind([](float Amount, AActor* Instigator)
{
    UE_LOG(LogGame, Log, TEXT("Damage: %.1f from %s"), Amount, *GetNameSafe(Instigator));
});
SomeEmitter->OnDamage.Connect(LoggingSlot);
```

### Emitting

```cpp
OnDamage.Emit(50.f, InstigatorActor);
// or
OnDamage(50.f, InstigatorActor);
```

### Disconnecting

```cpp
// Explicit disconnect
SomeEmitter->OnDamage.Disconnect(DamageSlot);

// Disconnect all slots from a signal
SomeEmitter->OnDamage.DisconnectAll();

// Automatic: slot destructor disconnects itself
// When DamageSlot goes out of scope or the owning object is destroyed,
// it automatically unlinks from its signal. No dangling pointers.
```

### Multi-Priority Signals

```cpp
// 4 priority levels: 0 (highest), 1, 2, 3 (lowest)
THybridSignal<FOnDamageDesc, 4> OnDamage;

// Connect at specific priorities
OnDamage.Connect(InterceptorSlot, 0);   // fires first
OnDamage.Connect(GameplaySlot, 1);      // fires second
OnDamage.Connect(LogSlot, 3);           // fires last
```

### Thread-Safe Signals

```cpp
// Critical section — use when signal emits from multiple threads
THybridSignal<FOnDamageDesc, 1, FCriticalSectionPolicy> OnDamage;

// Reader-writer lock — optimal when broadcasts >> registration changes
THybridSignal<FOnDamageDesc, 1, FReadWriteLockPolicy> OnDamage;

// Game-thread only (default) — zero overhead, debug assertion
THybridSignal<FOnDamageDesc> OnDamage;
```

---

## Bridge: Dual-Tier Emission

`TBridgedSignal` emits to Tier 2 direct slots first (synchronous, zero-alloc), then broadcasts through Tier 1 hub:

```cpp
// In header — note: FMessageStructType must be a USTRUCT for Tier 1 compatibility
TBridgedSignal<FDamageMessage> OnDamage;

// Direct slot connection (Tier 2 — fast path)
TMemberSlot<AMyComponent, TSignalArgDesc<FGameplayTag, const FDamageMessage&>> DirectSlot;
DirectSlot.Bind(this, &AMyComponent::OnDamageLocal);
OnDamage.Connect(DirectSlot);

// Emit to direct slots only
OnDamage.Emit(TAG_Combat_Damage, DamageMsg);

// Emit to direct slots AND broadcast through Tier 1 hub
OnDamage.EmitAndBroadcast(this, TAG_Combat_Damage, DamageMsg);
```

Use `TBridgedSignal` when the emitter's immediate owner needs zero-overhead notification (Tier 2) but remote systems also need to observe the event (Tier 1).

---

## Blueprint Integration

### Async Listen Node

The plugin provides a custom Blueprint node `Listen for Hybrid Messages`:

1. Drag from the execution flow and search for "Listen for Hybrid Messages"
2. Set the **Channel** (GameplayTag) and **Payload Type** (your USTRUCT)
3. The **OnMessageReceived** output fires each time a matching message arrives
4. Call **Get Payload** on the proxy object to read the message data
5. Set **Match Type** to `PartialMatch` for hierarchical listening
6. Set **Priority** to control execution order

The output **Payload** pin automatically matches the type you select in **Payload Type**.

### Signal Component

`UHybridSignalComponent` is a Blueprint-friendly actor component:

```
// In Blueprint:
1. Add "Hybrid Signal Component" to your Actor
2. Call StartListening(Channel, PayloadType, MatchType, Priority)
3. Bind to OnMessageReceived delegate
4. Call GetLastPayload() inside the delegate to read the message
5. Call BroadcastMessage(Channel, Message) to send
6. StopListening(Channel) or StopListeningAll() to clean up
```

All listeners are automatically cleaned up when the component's owning actor is destroyed.

### Blueprint Broadcast

Any Blueprint can broadcast through the subsystem:

1. Get a reference to the Hybrid Message Subsystem
2. Call `Broadcast Hybrid Message`
3. Pass the Channel tag and your message struct

---

## Thread Safety

### Tier 1 Hub

The subsystem uses an `FRWLock` internally:

- **Broadcast** acquires a read lock (multiple concurrent broadcasts are safe)
- **Register/Unregister** acquires a write lock (exclusive)
- **Callbacks execute outside the lock** — callbacks may safely call `RegisterListener` or `UnregisterListener` without deadlock
- The parent tag cache has its own `FCriticalSection`, independent of the listener map lock

**Safe operations from within a listener callback:**
- Registering new listeners (any channel)
- Unregistering listeners (any channel, including self)
- Broadcasting messages (any channel)
- Accessing the subsystem from async tasks

### Tier 2 Signals

Thread safety is a compile-time policy choice per signal:

| Policy | Overhead | Use Case |
|---|---|---|
| `FGameThreadOnlyPolicy` | Zero (debug assert only) | Default. Game-thread-only signals |
| `FCriticalSectionPolicy` | Per-signal mutex | Signals emitted from loading threads, async tasks |
| `FReadWriteLockPolicy` | Reader-writer lock | Signals with many concurrent emitters but rare connection changes |

**Important:** Slot `Bind()` and manual `Disconnect()` are **not** protected by the signal's lock. Ensure slots are fully bound before connecting to a signal, and disconnect before rebinding.

### Tier 2 Dispatch Safety

During `Emit()`, slots are snapshot into a stack-local buffer before invocation. If a slot disconnects during dispatch (e.g., a callback disconnects another slot), the dirty-buffer flag causes remaining dispatch iterations to validate each slot is still connected before invoking.

---

## Performance Characteristics

### Tier 1

| Operation | Complexity | Allocations |
|---|---|---|
| `BroadcastMessage` (N listeners) | O(N) | Zero heap for ≤16 listeners per channel (TInlineAllocator) |
| `RegisterListener` | O(N) insertion sort | One TArray append (amortized O(1)) |
| `UnregisterListener` | O(N) linear scan | None |
| Parent tag resolution | O(1) cached / O(D) first time | One cache entry per unique channel (D = tag depth) |
| Content filter evaluation | O(F) per broadcast | None (F = number of filtered listeners) |

### Tier 2

| Operation | Complexity | Allocations |
|---|---|---|
| `Connect` | O(1) | Zero (intrusive list) |
| `Disconnect` | O(1) | Zero (intrusive list) |
| `Emit` (N slots) | O(N) | Zero heap for ≤64 slots (TInlineAllocator) |
| `Emit` (0 slots) | ~0 | Zero (inlined `HasSlots()` check) |
| `GetSlotCount` | O(N) | None |

### Memory Overhead

- **Tier 1 per listener:** ~120 bytes (callback TFunction + metadata + content filter)
- **Tier 2 per slot:** ~40 bytes (3 pointers + priority + vtable)
- **Tier 2 per signal:** `sizeof(SlotType) * PriorityLevels + sizeof(ThreadPolicy)`
- **Subsystem:** One `TMap` for listeners, one `TMap` for parent tag cache, two locks

---

## Best Practices

### 1. Choose the Right Tier

Use **Tier 1** when:
- Communicating between systems that should not have compile-time dependencies
- You need hierarchical tag matching (analytics, quest systems, debug tools)
- Blueprint access is required
- Type covariance matters (base struct listeners receiving derived messages)

Use **Tier 2** when:
- The emitter and listener have an obvious ownership relationship
- Performance is critical (per-frame events, physics callbacks)
- You need compile-time type safety with zero overhead
- The connection topology is known at compile time

### 2. Use Appropriate Priorities

```cpp
// Interceptor: validate before game logic runs
Router.RegisterListener<FDamageMessage>(Channel, ValidationCallback,
    EHybridMessageMatch::ExactMatch, EHybridMessagePriority::First);

// Normal game logic
Router.RegisterListener<FDamageMessage>(Channel, GameplayCallback);

// Analytics: observe after all game logic
Router.RegisterListener<FDamageMessage>(Channel, AnalyticsCallback,
    EHybridMessageMatch::ExactMatch, EHybridMessagePriority::Last);
```

### 3. Use Content Filters for Expensive Callbacks

```cpp
// Instead of filtering inside the callback:
FHybridMessageListenerParams<FDamageMessage> Params;
Params.ContentFilter = [](const FDamageMessage& Msg) { return Msg.Amount > 0.f; };
Params.SetMessageReceivedCallback(this, &AMyClass::OnDamage);
Handle = Router.RegisterListener(Channel, Params);
```

### 4. Unregister When Done

```cpp
void AMyActor::EndPlay(const EEndPlayReason::Type Reason)
{
    Handle.Unregister();
    Super::EndPlay(Reason);
}
```

For Tier 2, store slots as members — their destructors handle disconnection automatically.

### 5. Use the Bridge When Both Tiers Need the Same Event

```cpp
TBridgedSignal<FDamageMessage> OnDamage;

// Local component gets zero-alloc direct dispatch
OnDamage.Connect(LocalSlot);

// Global analytics/quests/UI get notified through Tier 1
OnDamage.EmitAndBroadcast(this, TAG_Combat_Damage, Msg);
```

### 6. Keep Message Structs Small

Tier 1 copies the struct pointer into snapshots and passes `const void*`. Tier 2 passes arguments by value through the variadic template. For large payloads, use a pointer or shared reference inside the struct.

### 7. Use PartialMatch Sparingly

Partial matching walks the tag hierarchy and checks every parent channel. For high-frequency broadcasts, prefer `ExactMatch` and register on the specific channels you need.

### 8. Use the Debug Console Variable

```
HybridMessage.LogMessages 1
```

This logs every Tier 1 broadcast with the channel tag and serialized struct payload. Invaluable for debugging message flow.

---

## Anti-Patterns

### 1. DO NOT: Use Tier 1 for Per-Frame Events

```cpp
// BAD: Broadcasting 60x/sec through the central hub
void AMyActor::Tick(float DeltaTime)
{
    FPositionUpdate Msg;
    Msg.Location = GetActorLocation();
    Router.BroadcastMessage(TAG_Actor_PositionUpdate, Msg);  // unnecessary overhead
}
```

**Why:** Tier 1 acquires a read lock, snapshots listeners, walks parent tags, and checks type covariance on every broadcast. For per-frame communication, use a Tier 2 signal.

```cpp
// GOOD: Direct signal for per-frame updates
THybridSignal<TSignalArgDesc<FVector>> OnPositionChanged;
OnPositionChanged.Emit(GetActorLocation());
```

### 2. DO NOT: Hold Listener Handles in Transient Locals

```cpp
// BAD: Handle lost, listener leaks until subsystem shutdown
void AMyActor::SomeFunction()
{
    Router.RegisterListener<FDamageMessage>(Channel, ...);
    // Handle returned but not stored — no way to unregister
}
```

**Fix:** Store handles as class members and unregister in `EndPlay` or `BeginDestroy`.

### 3. DO NOT: Use PartialMatch on Root Tags

```cpp
// BAD: Matches every single message in the entire system
Handle = Router.RegisterListener<FBaseMessage>(
    FGameplayTag::RequestGameplayTag("Game"),
    Callback,
    EHybridMessageMatch::PartialMatch);
```

**Why:** Every broadcast on any `Game.*` tag will match this listener, causing massive dispatch overhead.

### 4. DO NOT: Store Raw Pointers to Tier 2 Slot Objects Across Frames

```cpp
// BAD: Object can be destroyed between frames
TMemberSlot<AEnemy, FOnDamageDesc>* SlotPtr = &Enemy->DamageSlot;
// ... next frame ...
SlotPtr->Disconnect();  // potential crash if Enemy was destroyed
```

**Fix:** Tier 2 slots auto-disconnect on destruction. If you need cross-frame references, use `TWeakObjectPtr` to the owning UObject and access the slot through it.

### 5. DO NOT: Mix Thread Policies Incorrectly

```cpp
// BAD: Signal is game-thread-only but emitted from async task
THybridSignal<TSignalArgDesc<int32>> OnDataReady;  // default = FGameThreadOnlyPolicy

AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [&]()
{
    OnDataReady.Emit(42);  // assertion failure in debug, undefined behavior in shipping
});
```

**Fix:** Use `FCriticalSectionPolicy` or `FReadWriteLockPolicy` for cross-thread signals:

```cpp
THybridSignal<TSignalArgDesc<int32>, 1, FCriticalSectionPolicy> OnDataReady;
```

### 6. DO NOT: Consume Messages Without Good Reason

```cpp
// BAD: Consuming at Normal priority silently breaks other listeners
Handle = Router.RegisterListener<FDamageMessage>(Channel,
    [](FGameplayTag, const FDamageMessage&) -> EHybridMessageResult
    {
        return EHybridMessageResult::Consumed;  // everything after this never fires
    },
    EHybridMessageMatch::ExactMatch,
    EHybridMessagePriority::Normal);  // other Normal listeners may be skipped
```

**Fix:** Only consume at `First` priority, and only for true interceptor patterns (input modal blocking, validation rejection).

### 7. DO NOT: Register Listeners in Broadcast Callbacks on the Same Channel

```cpp
// BAD: Creates listener during broadcast that may fire in the same dispatch cycle
Router.RegisterListener<FDamageMessage>(TAG_Combat_Damage,
    [&](FGameplayTag Channel, const FDamageMessage& Msg)
    {
        // This is safe (no deadlock) but the new listener won't see THIS message
        // because dispatch uses a snapshot. May cause confusing ordering issues.
        Router.RegisterListener<FDamageMessage>(TAG_Combat_Damage, AnotherCallback);
    });
```

**Why:** While technically safe (two-phase dispatch prevents deadlock), the newly registered listener only fires on the *next* broadcast. This creates subtle ordering bugs.

### 8. DO NOT: Use Tier 2 Signals as UObject UPROPERTY Members

```cpp
// BAD: THybridSignal is not a USTRUCT and cannot be reflected
UPROPERTY()
THybridSignal<TSignalArgDesc<float>> OnHealthChanged;  // compile error
```

**Fix:** Tier 2 signals are plain C++ members, not reflected properties. Declare them without `UPROPERTY()`.

### 9. DO NOT: Forget That TMemberSlot Uses Raw Pointers

```cpp
// BAD: Object destroyed while slot is still connected
TMemberSlot<AEnemy, FOnDamageDesc> Slot;
Slot.Bind(SomeEnemy, &AEnemy::OnDamage);
Signal.Connect(Slot);
SomeEnemy->Destroy();       // Slot.Object is now dangling
Signal.Emit(50.f, nullptr); // undefined behavior — invoking on destroyed object
```

**Fix:** Ensure the slot is disconnected (or destroyed) before the bound object is destroyed. Store the slot as a member of the bound object so their lifetimes match. For UObject safety, use Tier 1's weak-object `RegisterListener` overload instead.

### 10. DO NOT: Broadcast Empty or Default-Constructed Tags

```cpp
// BAD: Default FGameplayTag is invalid and matches nothing
FGameplayTag EmptyTag;
Router.BroadcastMessage(EmptyTag, Msg);  // silently dropped
```

**Fix:** Always use `FGameplayTag::RequestGameplayTag("Your.Tag.Name")` or tag constants.

---

## API Reference

### Enums

| Enum | Values | Description |
|---|---|---|
| `EHybridMessageMatch` | `ExactMatch`, `PartialMatch` | Whether a listener receives only exact channel matches or also child tags |
| `EHybridMessagePriority` | `First(0)`, `High(64)`, `Normal(128)`, `Low(192)`, `Last(255)` | Listener execution order (lower value = higher priority) |
| `EHybridMessageResult` | `Handled`, `Consumed`, `Skipped` | Callback return value. `Consumed` stops propagation |

### UHybridMessageSubsystem

| Method | Description |
|---|---|
| `Get(WorldContext)` | Returns subsystem reference (asserts on failure) |
| `HasInstance(WorldContext)` | Returns true if subsystem exists |
| `BroadcastMessage<T>(Channel, Message)` | Broadcast to all matching listeners |
| `RegisterListener<T>(Channel, Lambda, Match, Priority)` | Register lambda callback |
| `RegisterListener<T>(Channel, Object, MemberFunc, Match, Priority)` | Register weak member function |
| `RegisterListener<T>(Channel, ResultLambda, Match, Priority)` | Register result-returning callback |
| `RegisterListener<T>(Channel, Params)` | Register with advanced parameters |
| `UnregisterListener(Handle)` | Remove a listener |

### THybridSignal<ArgDesc, PriorityLevels, ThreadPolicy>

| Method | Description |
|---|---|
| `Connect(Slot, Priority)` | Connect a slot at the given priority level |
| `Disconnect(Slot)` | Disconnect a specific slot |
| `DisconnectAll()` | Disconnect all slots |
| `Emit(Args...)` | Dispatch to all connected slots in priority order |
| `operator()(Args...)` | Alias for `Emit` |
| `HasSlots()` | Returns true if any slot is connected |
| `GetSlotCount()` | Returns total connected slot count |

### Slot Types

| Type | Binds To |
|---|---|
| `TMemberSlot<TObject, ArgDesc>` | `void (TObject::*)(ArgTypes...)` |
| `TStaticSlot<ArgDesc>` | `void (*)(ArgTypes...)` |
| `TLambdaSlot<ArgDesc>` | `TFunction<void(ArgTypes...)>` |

All slot types support: `Bind(...)`, `IsConnected()`, `Disconnect()`, `GetPriority()`. Destructors auto-disconnect.

### TBridgedSignal<FMessageStructType, PriorityLevels, ThreadPolicy>

| Method | Description |
|---|---|
| `Connect(Slot, Priority)` | Connect a direct Tier 2 slot |
| `Disconnect(Slot)` | Disconnect a Tier 2 slot |
| `DisconnectAll()` | Disconnect all Tier 2 slots |
| `HasSlots()` | Check for connected Tier 2 slots |
| `Emit(Channel, Message)` | Dispatch to Tier 2 slots only |
| `EmitAndBroadcast(WorldContext, Channel, Message)` | Dispatch to Tier 2, then broadcast through Tier 1 |

### Thread Safety Policies

| Policy | Lock Type | When to Use |
|---|---|---|
| `FGameThreadOnlyPolicy` | None (debug assert) | Default — game-thread-only signals |
| `FCriticalSectionPolicy` | `FCriticalSection` per signal | Cross-thread emission |
| `FReadWriteLockPolicy` | `FRWLock` per signal | Many concurrent readers, rare writers |

---

## Migration from GameplayMessageRouter

HybridMessageRouter is a superset of Epic's GameplayMessageRouter. Migration is straightforward:

| Original | Replacement |
|---|---|
| `UGameplayMessageSubsystem` | `UHybridMessageSubsystem` |
| `EGameplayMessageMatch` | `EHybridMessageMatch` (same values) |
| `FGameplayMessageListenerHandle` | `FHybridMessageListenerHandle` |
| `#include "GameplayMessageSubsystem.h"` | `#include "HybridMessageSubsystem.h"` |
| `ListenForGameplayMessages` (Blueprint) | `ListenForHybridMessages` (Blueprint) |

All existing listener registrations work identically. The new plugin adds priority, consumption, content filtering, and Tier 2 signals on top.

---

## Module Structure

```
Plugins/HybridMessageRouter/
├── HybridMessageRouter.uplugin
└── Source/
    ├── HybridMessageRuntime/              (Runtime)
    │   ├── Public/
    │   │   ├── HybridMessageSubsystem.h       Tier 1 hub
    │   │   ├── HybridMessageTypes.h           Enums, handle, listener data
    │   │   ├── HybridSignal.h                 Tier 2 signal
    │   │   ├── HybridSlot.h                   Tier 2 slots (member, static, lambda)
    │   │   ├── HybridSignalArgDesc.h          Variadic argument descriptor
    │   │   ├── HybridSignalPolicies.h         Thread safety policies
    │   │   ├── HybridBridgedSignal.h          Tier 1 + 2 bridge
    │   │   ├── HybridSignalComponent.h        Blueprint actor component
    │   │   └── AsyncAction_ListenForHybridMessage.h
    │   └── Private/
    │       ├── HybridMessageSubsystem.cpp
    │       ├── HybridSignalComponent.cpp
    │       ├── AsyncAction_ListenForHybridMessage.cpp
    │       └── HybridMessageRuntimeModule.cpp
    └── HybridMessageNodes/                (UncookedOnly — editor only)
        ├── Public/
        │   └── K2Node_AsyncAction_ListenForHybridMessages.h
        └── Private/
            ├── K2Node_AsyncAction_ListenForHybridMessages.cpp
            └── HybridMessageNodesModule.cpp
```
