// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldWeatherContentLibrary.generated.h"

class UMaterialInterface;
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
};
