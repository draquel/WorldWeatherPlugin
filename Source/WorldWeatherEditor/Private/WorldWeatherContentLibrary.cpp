// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherContentLibrary.h"
#include "WorldWeatherEditor.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace
{
	FString RendererText(const UNiagaraRendererProperties* Renderer)
	{
		if (!Renderer)
		{
			return TEXT("null");
		}
		FString Material = TEXT("-");
		if (const UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
		{
			Material = Sprite->Material ? Sprite->Material->GetPathName() : TEXT("none");
		}
		else if (const UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			Material = FString::Printf(TEXT("%d mesh(es)"), Mesh->Meshes.Num());
		}
		return FString::Printf(TEXT("%s%s [%s]"), *Renderer->GetClass()->GetName(), Renderer->GetIsEnabled() ? TEXT("") : TEXT(" (disabled)"), *Material);
	}
}

bool UWorldWeatherContentLibrary::BuildPrecipitationSystem(UNiagaraSystem* System, UMaterialInterface* Material, bool bLocalSpace, FString& OutReport)
{
	OutReport.Reset();
	if (!System || !Material)
	{
		OutReport = TEXT("no system or no material");
		return false;
	}
	int32 Changed = 0;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		UNiagaraEmitter* Emitter = Instance.Emitter;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Emitter || !Data)
		{
			OutReport += FString::Printf(TEXT("%s: no emitter data\n"), *Handle.GetName().ToString());
			continue;
		}
		if (!Emitter->IsIn(System))
		{
			// A shared asset: changing it would change the pack's system too.
			OutReport += FString::Printf(TEXT("%s: the emitter %s is not the system's own; left alone\n"), *Handle.GetName().ToString(), *Emitter->GetPathName());
			continue;
		}
		Emitter->Modify();
		if (bLocalSpace)
		{
			Data->bLocalSpace = true;
		}
		int32 Sprites = 0;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Sprite->Modify();
				Sprite->Material = Material;
				++Sprites;
				++Changed;
			}
		}
		OutReport += FString::Printf(TEXT("%s: %s, local space %s, %d sprite renderer(s) given %s\n"), *Handle.GetName().ToString(),
			Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"), Data->bLocalSpace ? TEXT("on") : TEXT("off"), Sprites, *Material->GetName());
	}
	System->Modify();
	System->RequestCompile(/*bForce=*/true);
	System->MarkPackageDirty();
	UE_LOG(LogWorldWeatherEditor, Log, TEXT("BuildPrecipitationSystem %s:\n%s"), *System->GetName(), *OutReport);
	return Changed > 0;
}

FString UWorldWeatherContentLibrary::DescribeSystem(UNiagaraSystem* System)
{
	if (!System)
	{
		return TEXT("null");
	}
	FString Out = FString::Printf(TEXT("%s: %d emitter(s)\n"), *System->GetPathName(), System->GetEmitterHandles().Num());
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		const UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		Out += FString::Printf(TEXT("  %s%s: %s"), *Handle.GetName().ToString(), Handle.GetIsEnabled() ? TEXT("") : TEXT(" (disabled)"),
			Emitter ? (Emitter->IsIn(System) ? TEXT("owned") : *Emitter->GetPathName()) : TEXT("no emitter"));
		if (Data)
		{
			Out += FString::Printf(TEXT(", %s, local space %s, bounds %s, %d event handler(s)"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"),
				Data->bLocalSpace ? TEXT("on") : TEXT("off"), Data->CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Fixed ? TEXT("fixed") : TEXT("dynamic"), Data->GetEventHandlers().Num());
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				Out += TEXT("\n    renderer ") + RendererText(Renderer);
			}
		}
		Out += TEXT("\n");
	}
	TArray<FNiagaraVariable> Parameters;
	System->GetExposedParameters().GetParameters(Parameters);
	Out += TEXT("  user parameters:");
	for (const FNiagaraVariable& P : Parameters)
	{
		Out += FString::Printf(TEXT(" %s (%s)"), *P.GetName().ToString(), *P.GetType().GetName());
	}
	Out += TEXT("\n");
	return Out;
}

namespace
{
	/** Every script an emitter runs (the emitter and particle spawn and update), for its baked module constants. */
	TArray<UNiagaraScript*> EmitterScripts(FVersionedNiagaraEmitterData& Data)
	{
		TArray<UNiagaraScript*> Out;
		for (UNiagaraScript* Script : { Data.EmitterSpawnScriptProps.Script.Get(), Data.EmitterUpdateScriptProps.Script.Get(),
			Data.SpawnScriptProps.Script.Get(), Data.UpdateScriptProps.Script.Get() })
		{
			if (Script)
			{
				Out.AddUnique(Script);
			}
		}
		return Out;
	}

	/** Sets every rapid-iteration float constant whose name contains Needle to Value, in every script of the emitter. */
	int32 SetFloatConstant(FVersionedNiagaraEmitterData& Data, const FString& Needle, float Value, FString& OutReport)
	{
		int32 Set = 0;
		for (UNiagaraScript* Script : EmitterScripts(Data))
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				if (Parameter.GetName().ToString().Contains(Needle) && Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef())
				{
					Script->Modify();
					Script->RapidIterationParameters.SetParameterData(reinterpret_cast<const uint8*>(&Value), Parameter);
					++Set;
					OutReport += FString::Printf(TEXT("  set %s = %.2f\n"), *Parameter.GetName().ToString(), Value);
				}
			}
		}
		return Set;
	}

	/**
	 * Sets every rapid-iteration vector constant whose name contains Needle to Value (a position or a vector3), in
	 * every script of the emitter; returns how many it set.
	 */
	int32 SetBeamConstant(FVersionedNiagaraEmitterData& Data, const FString& Needle, const FVector3f& Value, FString& OutReport)
	{
		int32 Set = 0;
		for (UNiagaraScript* Script : EmitterScripts(Data))
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				if (!Parameter.GetName().ToString().Contains(Needle))
				{
					continue;
				}
				const FNiagaraTypeDefinition Type = Parameter.GetType();
				if (Type == FNiagaraTypeDefinition::GetPositionDef() || Type == FNiagaraTypeDefinition::GetVec3Def())
				{
					Script->Modify();
					Script->RapidIterationParameters.SetParameterData(reinterpret_cast<const uint8*>(&Value), Parameter);
					++Set;
					OutReport += FString::Printf(TEXT("  set %s = (%.0f, %.0f, %.0f)\n"), *Parameter.GetName().ToString(), Value.X, Value.Y, Value.Z);
				}
			}
		}
		return Set;
	}
}

bool UWorldWeatherContentLibrary::BuildLightningSystem(UNiagaraSystem* System, UNiagaraEmitter* TemplateEmitter, UMaterialInterface* Material, float BeamLength, float BoltSeconds, FString& OutReport)
{
	OutReport.Reset();
	if (!System || !Material)
	{
		OutReport = TEXT("no system or no material");
		return false;
	}
	if (System->GetEmitterHandles().Num() == 0)
	{
		if (!TemplateEmitter)
		{
			OutReport = TEXT("an empty system and no template emitter");
			return false;
		}
		System->Modify();
		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid, /*bCreateCopy=*/true);
		OutReport += FString::Printf(TEXT("added a copy of %s\n"), *TemplateEmitter->GetPathName());
	}
	bool bRibbon = false;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Emitter || !Data)
		{
			OutReport += FString::Printf(TEXT("%s: no emitter data\n"), *Handle.GetName().ToString());
			continue;
		}
		if (!Emitter->IsIn(System))
		{
			OutReport += FString::Printf(TEXT("%s: the emitter %s is not the system's own; left alone\n"), *Handle.GetName().ToString(), *Emitter->GetPathName());
			continue;
		}
		Emitter->Modify();
		Data->bLocalSpace = true;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
			{
				Ribbon->Modify();
				Ribbon->Material = Material;
				bRibbon = true;
				OutReport += FString::Printf(TEXT("%s: ribbon renderer -> %s\n"), *Handle.GetName().ToString(), *Material->GetPathName());
			}
		}
		const int32 Ends = SetBeamConstant(*Data, TEXT("Beam End"), FVector3f(0.f, 0.f, BeamLength), OutReport);
		const int32 Starts = SetBeamConstant(*Data, TEXT("Beam Start"), FVector3f::ZeroVector, OutReport);
		// A bolt is a blink: the beam's particles live BoltSeconds and the emitter loops no longer (the component also
		// deactivates the system when its light has faded, whatever the template's loop mode).
		const int32 Lives = SetFloatConstant(*Data, TEXT("InitializeParticle.Lifetime"), BoltSeconds, OutReport)
			+ SetFloatConstant(*Data, TEXT("EmitterState.Loop Duration"), BoltSeconds, OutReport);
		OutReport += FString::Printf(TEXT("%s: local space; beam constants set: %d end(s), %d start(s), %d lifetime(s)\n"), *Handle.GetName().ToString(), Ends, Starts, Lives);
		// Every constant, for the follow-up when the template's names differ.
		for (UNiagaraScript* Script : EmitterScripts(*Data))
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				OutReport += FString::Printf(TEXT("    constant %s : %s\n"), *Parameter.GetName().ToString(), *Parameter.GetType().GetName());
			}
		}
	}
	System->RequestCompile(/*bForce=*/true);
	return bRibbon;
}

namespace
{
	/** The constants that size a precipitation emitter's work (the spawn rate, the lifetime, the spawn box), as report text. */
	FString PrecipitationConstants(FVersionedNiagaraEmitterData& Data)
	{
		FString Out;
		for (UNiagaraScript* Script : EmitterScripts(Data))
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				const FString Name = Parameter.GetName().ToString();
				if (!Name.Contains(TEXT("SpawnRate.SpawnRate")) && !Name.Contains(TEXT("InitializeParticle.Lifetime")) && !Name.Contains(TEXT("BoxLocation.")))
				{
					continue;
				}
				const uint8* Value = Script->RapidIterationParameters.GetParameterData(Parameter);
				if (!Value)
				{
					continue;
				}
				if (Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef())
				{
					Out += FString::Printf(TEXT("  %s = %.2f\n"), *Name, *reinterpret_cast<const float*>(Value));
				}
				else if (Parameter.GetType() == FNiagaraTypeDefinition::GetVec3Def())
				{
					const FVector3f V = *reinterpret_cast<const FVector3f*>(Value);
					Out += FString::Printf(TEXT("  %s = (%.0f, %.0f, %.0f)\n"), *Name, V.X, V.Y, V.Z);
				}
			}
		}
		return Out;
	}
}

bool UWorldWeatherContentLibrary::SetPrecipitationGPUSimulation(UNiagaraSystem* System, float BoundsExtent, FString& OutReport)
{
	OutReport.Reset();
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	int32 OnGPU = 0;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		UNiagaraEmitter* Emitter = Instance.Emitter;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Emitter || !Data || !Source || !Source->NodeGraph)
		{
			OutReport += FString::Printf(TEXT("%s: no emitter data or graph\n"), *Handle.GetName().ToString());
			continue;
		}
		if (!Emitter->IsIn(System))
		{
			OutReport += FString::Printf(TEXT("%s: the emitter %s is not the system's own; left alone\n"), *Handle.GetName().ToString(), *Emitter->GetPathName());
			continue;
		}
		Emitter->Modify();
		Source->NodeGraph->Modify();
		const bool bWasGPU = Data->SimTarget == ENiagaraSimTarget::GPUComputeSim;
		Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		Data->FixedBounds = FBox(FVector(-BoundsExtent), FVector(BoundsExtent));

		// A GPU emitter writes no events: the modules that generate them are disabled (the stack keeps them, greyed).
		int32 Disabled = 0;
		TArray<UNiagaraNodeFunctionCall*> Calls;
		Source->NodeGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(Calls);
		for (UNiagaraNodeFunctionCall* Call : Calls)
		{
			if (Call->FunctionScript && Call->FunctionScript->GetName().Contains(TEXT("GenerateCollisionEvent")) && Call->IsNodeEnabled())
			{
				FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Call, false);
				++Disabled;
			}
		}

		// Nor reads them: the event handlers go, with their graph nodes (as the stack's delete does).
		TArray<FGuid> HandlerIds;
		for (const FNiagaraEventScriptProperties& Handler : Data->GetEventHandlers())
		{
			if (Handler.Script)
			{
				HandlerIds.Add(Handler.Script->GetUsageId());
			}
		}
		for (const FGuid& UsageId : HandlerIds)
		{
			TArray<UNiagaraNode*> HandlerNodes;
			Source->NodeGraph->BuildTraversal(HandlerNodes, ENiagaraScriptUsage::ParticleEventScript, UsageId);
			Emitter->RemoveEventHandlerByUsageId(UsageId, Instance.Version);
			for (UNiagaraNode* Node : HandlerNodes)
			{
				Node->Modify();
				Node->DestroyNode();
			}
		}
		++OnGPU;
		OutReport += FString::Printf(TEXT("%s: %s -> GPU, fixed bounds +-%.0f, %d collision-event module(s) disabled, %d event handler(s) removed\n%s"),
			*Handle.GetName().ToString(), bWasGPU ? TEXT("GPU") : TEXT("CPU"), BoundsExtent, Disabled, HandlerIds.Num(), *PrecipitationConstants(*Data));
	}
	System->Modify();
	System->RequestCompile(/*bForce=*/true);
	// Headless the script saves right after: the package must carry the GPU scripts, not the CPU ones.
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	UE_LOG(LogWorldWeatherEditor, Log, TEXT("SetPrecipitationGPUSimulation %s:\n%s"), *System->GetName(), *OutReport);
	return OnGPU > 0;
}
