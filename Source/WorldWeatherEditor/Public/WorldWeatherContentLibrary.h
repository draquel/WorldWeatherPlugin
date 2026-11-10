// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldWeatherContentLibrary.generated.h"

class UMaterialInterface;
class UNiagaraEmitter;
class UNiagaraSystem;

/**
 * Editor-only content helpers the weather's content script (the parent's Scripts/configure_weather.py) calls through
 * Python: the editor's Python cannot reach a Niagara system's emitters or renderers, so the precipitation systems (a
 * pack's rain and snow duplicated into the plugin's Content) get their sprite material and local space here.
 */
UCLASS()
class WORLDWEATHEREDITOR_API UWorldWeatherContentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Make a duplicated system the weather's: every sprite renderer of every emitter the system owns gets the material
	 * (the precipitation material, which fades with the rate and hides under the occlusion capture), every owned emitter
	 * is set to local space (the viewer rotates the component so the fall leans with the wind), and a compile is
	 * requested. An emitter the system does not own (a pack's shared asset) is left alone and named in the report.
	 * @param System The system to change (a duplicate in the plugin's Content, never the pack's own)
	 * @param Material The sprite material
	 * @param bLocalSpace Set the emitters to local space
	 * @param OutReport What was changed, one line per emitter
	 * @return true when at least one sprite renderer took the material
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Content")
	static bool BuildPrecipitationSystem(UNiagaraSystem* System, UMaterialInterface* Material, bool bLocalSpace, FString& OutReport);

	/**
	 * A system's shape, one line per emitter: its name, sim target, local space, each renderer's class and material, and
	 * the system's exposed user parameters.
	 * @param System The system
	 * @return The description
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Content")
	static FString DescribeSystem(UNiagaraSystem* System);

	/**
	 * Move a precipitation system's simulation to the GPU (G172): the pack's rain and snow simulate every particle on the
	 * CPU with the Collision module's scene traces, about 2.8 ms of game thread a frame for the snow. Every owned emitter
	 * is set to the GPU compute target with fixed local bounds (a GPU emitter cannot compute its bounds on the CPU), its
	 * collision-event modules are disabled and its event handlers removed (a GPU emitter neither writes nor reads
	 * events), and a compile is requested. The Collision module picks its GPU path (the depth buffer) by the target.
	 * @param System The system to change (a duplicate in the plugin's Content, never the pack's own)
	 * @param BoundsExtent The fixed bounds' half size, uu, about the component (the spawn box and a lifetime's fall)
	 * @param OutReport What was changed, one line per emitter, with the spawn rate and the lifetime it simulates
	 * @return true when at least one emitter simulates on the GPU
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Content")
	static bool SetPrecipitationGPUSimulation(UNiagaraSystem* System, float BoundsExtent, FString& OutReport);

	/**
	 * The bolt (the foliage and bolt plan's L1, D69): an empty system gets a copy of the engine's StaticBeam emitter
	 * template; every owned emitter is set to local space, its ribbon renderers take the bolt material, and the beam's
	 * end is written into the emitter's baked module constants (a rapid-iteration parameter whose name contains
	 * "Beam End") as BeamLength along the emitter's +Z, so the component orients the bolt along the local up at the
	 * strike. A compile is requested. The report lists what was set and every constant found, so a template whose
	 * names differ can be followed up.
	 * @param System The system (new or already built; a second call changes the values again)
	 * @param TemplateEmitter The emitter to copy in when the system has none (the engine's StaticBeam)
	 * @param Material The ribbon material
	 * @param BeamLength The bolt's length along +Z, uu (the cloud base's height above the strike)
	 * @param BoltSeconds How long the beam's particles live and the emitter loops (a blink)
	 * @param OutReport What was changed and the constants found
	 * @return true when the system has an emitter with a ribbon renderer carrying the material
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Content")
	static bool BuildLightningSystem(UNiagaraSystem* System, UNiagaraEmitter* TemplateEmitter, UMaterialInterface* Material, float BeamLength, float BoltSeconds, FString& OutReport);
};
