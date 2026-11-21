// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherProviders.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWorldWeatherClimateProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * What is specific to the world configuration, given to the weather by a game-side bridge (the plan's D60): the climate
 * and the local frame at a position, and the seed the schedule folds in. Without a provider every place is a Z-up plane
 * at the settings' temperature and moisture.
 */
class WORLDWEATHER_API IWorldWeatherClimateProvider
{
	GENERATED_BODY()

public:
	/**
	 * The climate at a position.
	 * @param WorldPos Position
	 * @param OutSample Filled when known
	 * @return false when the provider knows nothing there (the subsystem uses its defaults)
	 */
	virtual bool GetClimateAt(const FVector& WorldPos, FWorldWeatherClimateSample& OutSample) const = 0;

	/** The world's seed, folded into the schedule's (0 = none). */
	virtual int32 GetWeatherSeed() const { return 0; }
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWorldWeatherShelterProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shelter the world knows about that a trace cannot see (a dungeon's interior, a camp's claim). The subsystem asks it
 * first and traces the sky when it has no answer.
 */
class WORLDWEATHER_API IWorldWeatherShelterProvider
{
	GENERATED_BODY()

public:
	/**
	 * The exposure at a position.
	 * @param WorldPos Position
	 * @param Up The local up
	 * @param OutExposure 1 under the open sky, 0 fully sheltered
	 * @return false when the provider knows nothing there (the subsystem traces)
	 */
	virtual bool GetExposureAt(const FVector& WorldPos, const FVector& Up, float& OutExposure) const = 0;
};
