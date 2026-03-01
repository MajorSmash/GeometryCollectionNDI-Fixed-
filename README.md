# GeoCollectionNDI Fix

Drop-in replacement for `UNiagaraDataInterfaceGeometryCollection` that fixes per-frame heap memory leaks when used with GPU Niagara emitters. Temporary workaround until Epic ships an engine fix.

## The Problem

The engine's Geometry Collection Data Interface (`ChaosNiagara` plugin) leaks heap memory every frame when used with GPU emitters. The root cause is `FNDIGeometryCollectionArrays` being stored as a raw pointer that gets `new`'d each frame in `ProvidePerInstanceDataForRenderThread` but never freed when overwritten. Secondary leaks exist in `Init()` (no cleanup before reallocation) and at destruction (game-thread `AssetArrays` never freed).

## The Fix

The comparable `NiagaraDataInterfacePhysicsAsset` avoids all of this by storing its equivalent arrays struct by value. This plugin does the same. See `DESIGN_GeoCollectionNDI_MemLeakFix.md` for full technical details.

## Compatibility

- Compiles and runs on **UE 5.6** and **UE 5.7**
- No engine modifications required

**Note:** When switching between engine versions or recompiling after a failed build, you may need to delete the `Binaries` and `Intermediate` folders inside the plugin's directory before it will successfully compile.

## Installation

1. Copy the `Plugins/GeoCollectionNDIFix` folder into your project's `Plugins/` directory
1.1. If no Plugin folder already exists, you can add one in the `Root` folder (same one `Content` is in).
2. Rebuild your project (or restart the editor and let it compile)
3. Enable the **GeoCollectionNDIFix** plugin in Edit > Plugins if it isn't enabled automatically

## Usage

1. Open any Niagara system that uses the Geometry Collection Data Interface
2. In the Parameters panel, find the parameter of type **Geometry Collection**
3. Change its type to **Geometry Collection (Fixed)** (it sits right next to the original in the list)
4. Configure the same properties you had before (Source Mode, Default Geometry Collection, Source Actor, etc.)
5. All function names and signatures are identical -- no rewiring needed

## Removal

When Epic fixes the engine DI:

1. Swap each **Geometry Collection (Fixed)** back to **Geometry Collection** in your Niagara systems
2. Disable or delete the plugin
