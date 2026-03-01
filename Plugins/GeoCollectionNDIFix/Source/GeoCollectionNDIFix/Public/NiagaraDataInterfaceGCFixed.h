// Drop-in replacement for UNiagaraDataInterfaceGeometryCollection with memory leak fixes.
// See DESIGN_GeoCollectionNDI_MemLeakFix.md for details.

#pragma once

#include "NiagaraDataInterface.h"
#include "RHIUtilities.h"
#include "GeometryCollection/GeometryCollectionObject.h"

#include "NiagaraDataInterfaceGCFixed.generated.h"

class AGeometryCollectionActor;
struct FNiagaraDataInterfaceGeneratedFunction;

// CPU-side arrays mirroring FNDIGeometryCollectionArrays
struct FNDIGCFixedArrays
{
	TArray<FVector4f> WorldTransformBuffer;
	TArray<FVector4f> PrevWorldTransformBuffer;
	TArray<FVector4f> WorldInverseTransformBuffer;
	TArray<FVector4f> PrevWorldInverseTransformBuffer;
	TArray<FVector4f> BoundsBuffer;
	TArray<FTransform> ComponentRestTransformBuffer;
	TArray<uint32> ElementIndexToTransformBufferMapping;

	FNDIGCFixedArrays()
	{
		Resize(100);
	}

	FNDIGCFixedArrays(uint32 Num)
	{
		Resize(Num);
	}

	void CopyFrom(const FNDIGCFixedArrays& Other)
	{
		Resize(Other.NumPieces);

		WorldTransformBuffer = Other.WorldTransformBuffer;
		PrevWorldTransformBuffer = Other.PrevWorldTransformBuffer;
		WorldInverseTransformBuffer = Other.WorldInverseTransformBuffer;
		PrevWorldInverseTransformBuffer = Other.PrevWorldInverseTransformBuffer;
		BoundsBuffer = Other.BoundsBuffer;
		ComponentRestTransformBuffer = Other.ComponentRestTransformBuffer;
		ElementIndexToTransformBufferMapping = Other.ElementIndexToTransformBufferMapping;
	}

	void Resize(uint32 Num)
	{
		NumPieces = Num;

		WorldTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		PrevWorldTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		WorldInverseTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		PrevWorldInverseTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		BoundsBuffer.Init(FVector4f(0, 0, 0, 0), NumPieces);
		ComponentRestTransformBuffer.Init(FTransform(), 1);
		ElementIndexToTransformBufferMapping.Init(0, NumPieces);
	}

	uint32 NumPieces = 100;
};

// GPU render buffers mirroring FNDIGeometryCollectionBuffer
struct FNDIGCFixedBuffer : public FRenderResource
{
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
	virtual FString GetFriendlyName() const override { return TEXT("FNDIGCFixedBuffer"); }

	FReadBuffer WorldTransformBuffer;
	FReadBuffer PrevWorldTransformBuffer;
	FReadBuffer WorldInverseTransformBuffer;
	FReadBuffer PrevWorldInverseTransformBuffer;
	FReadBuffer BoundsBuffer;

	TRefCountPtr<FRDGPooledBuffer> ComponentRestTransformBuffer;
	TArray<uint8> DataToUpload;

	int32 NumPieces;

	void SetNumPieces(int32 Num)
	{
		NumPieces = Num;
	}
};

struct FResolvedNiagaraGCFixed
{
	const UGeometryCollection* GetGeometryCollection() const;
	FTransform GetComponentRootTransform(FNiagaraSystemInstance* SystemInstance) const;
	FTransform GetComponentSpaceTransform(int32 TransformIndex) const;
	TArray<FTransform> GetLocalRestTransforms() const;

	TWeakObjectPtr<UGeometryCollection> Collection;
	TWeakObjectPtr<UGeometryCollectionComponent> Component;
};

// Per-instance data — FIX: AssetArrays stored by value, not raw pointer
struct FNDIGCFixedData
{
	void Init(class UNiagaraDataInterfaceGCFixed* Interface, FNiagaraSystemInstance* SystemInstance);
	void Update(class UNiagaraDataInterfaceGCFixed* Interface, FNiagaraSystemInstance* SystemInstance);
	void Release();

	ETickingGroup ComputeTickingGroup();

	ETickingGroup TickingGroup;
	FTransform RootTransform;
	FVector3f BoundsOrigin;
	FVector3f BoundsExtent;

	FNDIGCFixedBuffer* AssetBuffer = nullptr;
	FNDIGCFixedArrays AssetArrays;                     // FIX: by value, not pointer

	bool bHasPendingComponentTransformUpdate = false;
	bool bNeedsRenderUpdate = false;

	FResolvedNiagaraGCFixed ResolvedSource;
};

// Re-use the same source mode enum (not generated, just matches engine values)
UENUM()
enum class ENDIGCFixed_SourceMode : uint8
{
	Default,
	Source,
	AttachParent,
	DefaultCollectionOnly,
	ParameterBinding,
};

UCLASS(EditInlineNew, Category = "Chaos", meta = (DisplayName = "Geometry Collection (Fixed)"), MinimalAPI)
class UNiagaraDataInterfaceGCFixed : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(FVector3f,				BoundsMin)
		SHADER_PARAMETER(FVector3f,				BoundsMax)
		SHADER_PARAMETER(int32,					NumPieces)
		SHADER_PARAMETER(FVector3f,				RootTransform_Translation)
		SHADER_PARAMETER(FQuat4f,				RootTransform_Rotation)
		SHADER_PARAMETER(FVector3f,				RootTransform_Scale)
		SHADER_PARAMETER_SRV(Buffer<float4>,	WorldTransformBuffer)
		SHADER_PARAMETER_SRV(Buffer<float4>,	PrevWorldTransformBuffer)
		SHADER_PARAMETER_SRV(Buffer<float4>,	WorldInverseTransformBuffer)
		SHADER_PARAMETER_SRV(Buffer<float4>,	PrevWorldInverseTransformBuffer)
		SHADER_PARAMETER_SRV(Buffer<float4>,	BoundsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,	ElementTransforms)
	END_SHADER_PARAMETER_STRUCT()

	UPROPERTY(EditAnywhere, Category = "Geometry Collection")
	ENDIGCFixed_SourceMode SourceMode = ENDIGCFixed_SourceMode::Default;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Geometry Collection")
	TSoftObjectPtr<UGeometryCollection> PreviewCollection;
#endif

	UPROPERTY(EditAnywhere, Category = "Geometry Collection", meta = (EditConditionHides, EditCondition = "SourceMode == ENDIGCFixed_SourceMode::Default || SourceMode == ENDIGCFixed_SourceMode::DefaultCollectionOnly"))
	TObjectPtr<UGeometryCollection> DefaultGeometryCollection;

	UPROPERTY(EditAnywhere, Category = "Geometry Collection", meta = (DisplayName = "Source Actor", EditConditionHides, EditCondition = "SourceMode == ENDIGCFixed_SourceMode::Default || SourceMode == ENDIGCFixed_SourceMode::Source"))
	TSoftObjectPtr<AGeometryCollectionActor> GeometryCollectionActor;

	UPROPERTY(Transient)
	TObjectPtr<UGeometryCollectionComponent> SourceComponent;

	UPROPERTY(EditAnywhere, Category = "Geometry Collection", meta = (EditConditionHides, EditCondition = "SourceMode == ENDIGCFixed_SourceMode::Default || SourceMode == ENDIGCFixed_SourceMode::ParameterBinding"))
	FNiagaraUserParameterBinding GeometryCollectionUserParameter;

	UPROPERTY(EditAnywhere, Category = "Geometry Collection")
	bool bIncludeIntermediateBones = false;

	virtual void PostInitProperties() override;

	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(FNDIGCFixedData); }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }
	virtual bool PostSimulateCanOverlapFrames() const override { return false; }
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override;

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

private:
	void GetNumGeometryElements(FVectorVMExternalFunctionContext& Context);
	void GetElementBounds(FVectorVMExternalFunctionContext& Context);
	void GetElementTransformCS(FVectorVMExternalFunctionContext& Context);
	void SetElementTransformCS(FVectorVMExternalFunctionContext& Context);
	void SetElementTransformWS(FVectorVMExternalFunctionContext& Context);
	void GetActorTransform(FVectorVMExternalFunctionContext& Context);

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

private:
	void ResolveGeometryCollection(FNiagaraSystemInstance* SystemInstance, FNDIGCFixedData* InstanceData);
	bool ResolveGeometryCollectionFromDirectSource(FResolvedNiagaraGCFixed& ResolvedSource);
	bool ResolveGeometryCollectionFromAttachParent(FNiagaraSystemInstance* SystemInstance, FResolvedNiagaraGCFixed& ResolvedSource);
	bool ResolveGeometryCollectionFromActor(AActor* Actor, FResolvedNiagaraGCFixed& ResolvedSource);
	bool ResolveGeometryCollectionFromDefaultCollection(FResolvedNiagaraGCFixed& ResolvedSource);
	bool ResolveGeometryCollectionFromParameterBinding(UObject* UserParameter, FResolvedNiagaraGCFixed& ResolvedSource);
};

// Proxy — FIX: ConsumePerInstanceDataFromGameThread copies AssetArrays by value
struct FNDIGCFixedProxy : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIGCFixedData); }
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;
	virtual void PreStage(const FNDIGpuComputePreStageContext& Context) override;

	TMap<FNiagaraSystemInstanceID, FNDIGCFixedData> SystemInstancesToProxyData;
};
