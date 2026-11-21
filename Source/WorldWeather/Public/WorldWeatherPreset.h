// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherPreset.generated.h"

/**
 * One weather a world can have: its tag, its front, how long it holds and blends (game minutes), and its affinities, a
 * weight by temperature band and by moisture band, so the schedule in a desert rarely draws Rain and in the tundra draws
 * Snow. A weight of 0 in a band zeroes the preset out there (the bands blend linearly between 0, 0.5 and 1).
 */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherPreset
{
	GENERATED_BODY()

	/** Weather.Preset.* (what consumers match; one per row). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset", meta = (Categories = "Weather.Preset"))
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FWorldWeatherFront Front;

	/** The shortest hold after the blend, game minutes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0"))
	float HoldMinutesMin = 60.0f;

	/** The longest hold after the blend, game minutes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0"))
	float HoldMinutesMax = 180.0f;

	/** How long the blend into this preset takes, game minutes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0"))
	float BlendMinutes = 10.0f;

	/** Weight where the climate's temperature is 0 (the coldest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float ColdWeight = 1.0f;

	/** Weight at temperature 0.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float TemperateWeight = 1.0f;

	/** Weight at temperature 1 (the hottest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float HotWeight = 1.0f;

	/** Weight where the climate's moisture is 0 (the driest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float DryWeight = 1.0f;

	/** Weight at moisture 0.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float ModerateWeight = 1.0f;

	/** Weight at moisture 1 (the wettest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity", meta = (ClampMin = "0"))
	float WetWeight = 1.0f;

	/**
	 * The preset's weight at a climate: the temperature band's weight times the moisture band's.
	 * @param Temperature Dimensionless 0..1
	 * @param Moisture Dimensionless 0..1
	 */
	float WeightAt(float Temperature, float Moisture) const;
};

/**
 * The fronts a world can have. The settings name one; without it the subsystem builds the defaults (Clear, Overcast, Rain,
 * Storm, Snow, Fog, Windy) in memory, so a world needs no asset to have weather.
 */
UCLASS(BlueprintType)
class WORLDWEATHER_API UWorldWeatherPresetTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TArray<FWorldWeatherPreset> Presets;

	/** The row with a tag; null when none. */
	const FWorldWeatherPreset* FindByTag(const FGameplayTag& Tag) const;

	/** The row's index for a tag, or -1. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Presets")
	int32 IndexOfTag(const FGameplayTag& Tag) const;

	/** Replace the rows with the seven defaults (what a world without a table gets; the content script bakes them into an asset). */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Presets")
	void ResetToDefaults();

	/** The seven default presets. */
	static void FillDefaults(TArray<FWorldWeatherPreset>& OutPresets);
};
