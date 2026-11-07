// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherContentLibrary.h"
#include "WorldWeatherEditor.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"

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
			Out += FString::Printf(TEXT(", %s, local space %s"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"), Data->bLocalSpace ? TEXT("on") : TEXT("off"));
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
