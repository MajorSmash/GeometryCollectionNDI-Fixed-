# Plugin Design Document: GeoCollectionNDI Memory Leak Fix

## Problem Statement

`UNiagaraDataInterfaceGeometryCollection` in the ChaosNiagara plugin has a per-frame heap memory leak when used with GPU Niagara emitters. The root cause is that `FNDIGeometryCollectionArrays` is stored as a raw pointer and heap-allocated every frame via `ProvidePerInstanceDataForRenderThread`, but the previous frame's pointer is never freed when overwritten in `ConsumePerInstanceDataFromGameThread`. Secondary leaks exist in `Init()` (no cleanup before reallocation) and at destruction (game-thread `AssetArrays` never freed).

The comparable `NiagaraDataInterfacePhysicsAsset` avoids all of this by storing its equivalent arrays struct **by value**.

## Design Philosophy

- **DRY / KISS / YAGNI**: Replicate the existing engine DI's public interface 1:1. Fix only the memory management. No new features, no refactors beyond what the fix requires.
- **Drop-in replacement**: Same Niagara function names, same shader interface, same UPROPERTY bindings. Users swap the DI type in their Niagara systems; everything else stays the same.
- **Minimal engine coupling**: The plugin depends on the same modules as ChaosNiagara. No engine modifications required.
- **Forward-compatible**: When Epic fixes the engine DI, users remove this plugin and switch back. No migration pain.

## Plugin Structure

```
Plugins/
  GeoCollectionNDIFix/
    GeoCollectionNDIFix.uplugin
    Source/
      GeoCollectionNDIFix/
        GeoCollectionNDIFix.Build.cs
        Public/
          NiagaraDataInterfaceGCFixed.h
        Private/
          GeoCollectionNDIFixModule.cpp
          NiagaraDataInterfaceGCFixed.cpp
    Shaders/
      NiagaraDataInterfaceGCFixed.ush
```

## The Fix (3 Changes)

All three bugs share the same root cause: `AssetArrays` is a raw pointer requiring manual lifecycle management in multiple locations. The PhysicsAsset DI solves this by storing the struct by value. We do the same.

### Change 1: Store `AssetArrays` by value instead of pointer

**Engine code (broken):**
```cpp
struct FNDIGeometryCollectionData
{
    FNDIGeometryCollectionBuffer* AssetBuffer = nullptr;
    FNDIGeometryCollectionArrays* AssetArrays = nullptr;   // raw pointer
};
```

**Fixed code:**
```cpp
struct FNDIGCFixedData
{
    FNDIGCFixedBuffer* AssetBuffer = nullptr;
    FNDIGCFixedArrays AssetArrays;                         // by value
};
```

This single change eliminates:
- **Bug 1** (per-frame leak): `ProvidePerInstanceDataForRenderThread` no longer calls `new`. It copies the struct into the pre-allocated render thread buffer. `ConsumePerInstanceDataFromGameThread` overwrites by value. No pointer, no leak.
- **Bug 2** (`Init()` leak): `AssetArrays` is just `.Resize()`d in place. No `new` to orphan.
- **Bug 3** (destruction leak): Value member is destroyed automatically with its parent struct.

**Critical implementation detail:** `ProvidePerInstanceDataForRenderThread` receives `DataForRenderThread` as **raw uninitialized memory** (allocated by Niagara with size `PerInstanceDataPassedToRenderThreadSize()`). With pointer-based `AssetArrays`, the engine could `static_cast` and immediately write into it — a garbage pointer just gets overwritten. With by-value `AssetArrays`, the `TArray` members must be properly constructed before use, otherwise `CopyFrom` → `Resize` → `TArray::Init()` tries to free garbage allocator pointers and crashes (`EXCEPTION_ACCESS_VIOLATION`). The fix is to **placement-new** the render thread buffer:

```cpp
// Engine code: static_cast is safe because AssetArrays is just a pointer being overwritten
FNDIGeometryCollectionData* RenderThreadData = static_cast<FNDIGeometryCollectionData*>(DataForRenderThread);

// Fixed code: placement-new required because by-value TArrays need construction
FNDIGCFixedData* RenderThreadData = new (DataForRenderThread) FNDIGCFixedData();
```

### Change 2: `Init()` must call `Release()` before reinitializing

**Engine code (broken):**
```cpp
void FNDIGeometryCollectionData::Init(...)
{
    AssetBuffer = nullptr;   // old GPU resource abandoned
    // ...
    AssetBuffer = new FNDIGeometryCollectionBuffer();
}
```

**Fixed code:**
```cpp
void FNDIGCFixedData::Init(...)
{
    Release();               // properly release old GPU resource via render command
    // ...
    AssetBuffer = new FNDIGCFixedBuffer();
    BeginInitResource(AssetBuffer);
}
```

### Change 3: `Release()` is complete (no separate cleanup path needed)

The engine splits `AssetArrays` cleanup between `Release()` (doesn't touch it) and `DestroyPerInstanceData` (render command deletes it). With by-value storage, `Release()` only needs to handle `AssetBuffer` (the GPU resource), and `AssetArrays` is cleaned up automatically.

The engine's `FNDIGeometryCollectionProxy::DestroyPerInstanceData` and `InitializePerInstanceData` are dead code (defined but never called). We omit them.

## What We Replicate (1:1)

Everything below is carried over from the engine DI unchanged in behavior. The only differences are struct/class names and the memory management fixes above.

### Niagara Functions (7 total)

| Function Name | CPU | GPU | Description |
|---|---|---|---|
| `GetClosestPointNoNormal` | No | Yes | Closest point on GC surface. Loops all pieces on GPU. |
| `GetNumElements` | Yes | Yes | Piece count |
| `GetElementBounds` | Yes | Yes | Per-element bounding box center + size |
| `GetElementTransform` | Yes | Yes | Per-element translation, rotation, scale (component space) |
| `SetElementTransform` | Yes | No | Set element transform (component space) |
| `SetElementTransformWS` | Yes | No | Set element transform (world space) |
| `GetGeometryComponentTransform` | Yes | Yes | Root component world transform |

### GPU Shader Parameters

Replicated from the engine's `FShaderParameters` and `.ush` file:

```
BoundsMin, BoundsMax           (float3)
NumPieces                      (int)
RootTransform_Translation      (float3)
RootTransform_Rotation         (float4 / quat)
RootTransform_Scale            (float3)
WorldTransformBuffer           (Buffer<float4>)
PrevWorldTransformBuffer       (Buffer<float4>)
WorldInverseTransformBuffer    (Buffer<float4>)
PrevWorldInverseTransformBuffer (Buffer<float4>)
BoundsBuffer                   (Buffer<float4>)
ElementTransforms              (ByteAddressBuffer)
```

### GPU Render Buffers (`FNDIGCFixedBuffer`)

Same as engine's `FNDIGeometryCollectionBuffer`:
- `FReadBuffer` for world/prev/inverse/prev-inverse transforms and bounds
- `TRefCountPtr<FRDGPooledBuffer>` for `ComponentRestTransformBuffer`
- `TArray<uint8> DataToUpload` for staging transform data
- `InitRHI` / `ReleaseRHI` lifecycle

### CPU Arrays (`FNDIGCFixedArrays`)

Same as engine's `FNDIGeometryCollectionArrays`:
- `TArray<FVector4f>` for world, prev-world, inverse, prev-inverse transforms (3 * NumPieces each)
- `TArray<FVector4f>` for bounds (NumPieces)
- `TArray<FTransform>` for component rest transforms
- `TArray<uint32>` for element-to-transform index mapping
- `Resize()` and `CopyFrom()` methods

### Source Resolution

Same `ENDIGeometryCollection_SourceMode` enum with identical resolution chain:
- Default (Source -> ParameterBinding -> AttachParent -> DefaultCollection)
- Source only
- AttachParent only
- DefaultCollectionOnly
- ParameterBinding only

Editor-only `PreviewCollection` fallback for non-game worlds.

### Proxy (`FNDIGCFixedProxy`)

Same pattern as engine proxy but with the fix applied:
- `TMap<FNiagaraSystemInstanceID, FNDIGCFixedData> SystemInstancesToProxyData`
- `ConsumePerInstanceDataFromGameThread`: copies by value (no `new`, no leak)
- `PreStage`: uploads transform/bounds data to GPU buffers via `UpdateInternalBuffer` and RDG buffer upload

### Per-Instance Tick

Same as engine:
- `PerInstanceTick`: resolves GC source, calls `Update()`
- `PerInstanceTickPostSimulate`: pushes pending transform writes back to component via `SetLocalRestTransforms`
- `HasPreSimulateTick = true`, `HasPostSimulateTick = true`, `PostSimulateCanOverlapFrames = false`

### Shader File (`NiagaraDataInterfaceGCFixed.ush`)

Byte-for-byte identical HLSL logic to the engine's `.ush`, with `{ParameterName}` template substitution. All GPU functions:
- `GetClosestPointNoNormal` (box projection loop over all pieces)
- `GetNumElements`
- `GetElementTransform` (loads from `ByteAddressBuffer` via `LoadTransform`)
- `GetElementBounds`
- `GetGeometryComponentTransform`

The shader path changes to `/Plugin/GeoCollectionNDIFix/NiagaraDataInterfaceGCFixed.ush`.

## Module Dependencies

```csharp
// GeoCollectionNDIFix.Build.cs
PublicDependencyModuleNames:
  Core, CoreUObject, Niagara, NiagaraCore, NiagaraShader,
  NiagaraVertexFactories, VectorVM, RHI, RenderCore,
  Chaos, GeometryCollectionEngine

PrivateDependencyModuleNames:
  Engine, Projects
```

**Why `Chaos` is required:** The core geometry collection data types (`FGeometryCollection`, `GeometryCollectionAlgo`, `TManagedArray`) live in the `Chaos` module, not in `GeometryCollectionEngine`. `GeometryCollectionEngine` does expose `Chaos` transitively (via `SetupModulePhysicsSupport`), but we list it explicitly because we directly include its headers.

**What we don't need:**
- `ChaosNiagara` — we are replacing its DI, not extending it.
- `ChaosSolverEngine` — no solver types used. Flows through `GeometryCollectionEngine` if needed.
- `FieldSystemEngine` — no field system types used.
- `Slate` / `SlateCore` — used by ChaosDestruction DI, not the GC DI.
- `CHAOS_INCLUDE_LEVEL_1` define — `PrivateDefinitions` in all modules that set it. Does not gate any public headers (verified: zero matches in Chaos public includes).

## Class Naming

| Engine Class | Plugin Class |
|---|---|
| `UNiagaraDataInterfaceGeometryCollection` | `UNiagaraDataInterfaceGCFixed` |
| `FNDIGeometryCollectionData` | `FNDIGCFixedData` |
| `FNDIGeometryCollectionArrays` | `FNDIGCFixedArrays` |
| `FNDIGeometryCollectionBuffer` | `FNDIGCFixedBuffer` |
| `FNDIGeometryCollectionProxy` | `FNDIGCFixedProxy` |
| `FResolvedNiagaraGeometryCollection` | `FResolvedNiagaraGCFixed` |

Display name in Niagara: `"Geometry Collection (Fixed)"` so it sits adjacent to the broken one in the DI picker.

## What We Omit

- `FNDIGeometryCollectionProxy::InitializePerInstanceData()` and `DestroyPerInstanceData()` — dead code in engine, never called.
- `UpgradeFunctionCall` version migration from pre-`AddedElementIndexOutput` — new plugin has no legacy data to migrate.
- The `CHAOS_INCLUDE_LEVEL_1` define — not needed; we don't depend on Chaos solver internals.

## Usage

1. Enable the `GeoCollectionNDIFix` plugin.
2. In any Niagara system using the Geometry Collection DI, swap `"Geometry Collection"` for `"Geometry Collection (Fixed)"`.
3. All function names and parameters are identical. No rewiring needed.
4. When Epic ships a fix, reverse step 2 and disable the plugin.
