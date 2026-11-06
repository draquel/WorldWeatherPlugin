// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherSettings.generated.h"

class UCurveFloat;
class UWorldWeatherPresetTable;

/**
 * Weather configuration (Project Settings > Plugins > World Weather; DefaultGame.ini
 * [/Script/WorldWeather.WorldWeatherSettings]). Read by the subsystem at world start and on every query.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Weather"))
class WORLDWEATHER_API UWorldWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldWeatherSettings();

	// --- The schedule ---

	/** The fronts the world can have. Empty: the seven defaults (UWorldWeatherPresetTable::FillDefaults). */
	UPROPERTY(Config, EditAnywhere, Category = "Schedule")
	TSoftObjectPtr<UWorldWeatherPresetTable> PresetTable;

	/** Folded into the schedule with the climate provider's world seed: the same seed, day and index give the same fronts. */
	UPROPERTY(Config, EditAnywhere, Category = "Schedule")
	int32 ScheduleSeed = 0;

	/** Draw fronts from the schedule as holds run out. Off: the weather stays at StartPreset (or the first row) until set. */
	UPROPERTY(Config, EditAnywhere, Category = "Schedule")
	bool bAutoSchedule = true;

	/** The first front (blended in at once). None: the schedule's first draw. */
	UPROPERTY(Config, EditAnywhere, Category = "Schedule", meta = (Categories = "Weather.Preset"))
	FGameplayTag StartPreset;

	// --- Temperature (the plan's D66: the climate's dimensionless temperature to degrees) ---

	/** Degrees C where the climate's temperature is 0. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature")
	float ColdTemperatureC = -20.0f;

	/** Degrees C where the climate's temperature is 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature")
	float HotTemperatureC = 35.0f;

	/** Optional curve from the climate's temperature (0..1) to degrees C, in place of the linear map above. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature")
	TSoftObjectPtr<UCurveFloat> TemperatureCurve;

	/** How much colder a full night is than noon, degrees C (scaled by 1 - daylight at the place). */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0"))
	float NightCoolingC = 6.0f;

	/** The temperature and moisture a world without a climate provider has everywhere (0..1). */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0", ClampMax = "1"))
	float DefaultTemperature = 0.55f;

	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0", ClampMax = "1"))
	float DefaultMoisture = 0.5f;

	// --- Precipitation ---

	/** Precipitation falls as snow at or under this temperature, degrees C. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation")
	float SnowBelowC = 0.0f;

	/** Sleet this many degrees over SnowBelowC; rain above. Also the band over which the snow cover gives way to wetness. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0"))
	float SleetBandC = 2.0f;

	/** The precipitation rate where the climate's moisture is 0, as a fraction of the front's (1 where it is 1). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0", ClampMax = "1"))
	float DryPrecipitationScale = 0.25f;

	/** Game seconds of rate-1 precipitation to soak dry ground. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "1"))
	float WetnessRiseSeconds = 600.0f;

	/** Game seconds for soaked ground to dry in dry air (humid air dries up to half as fast). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "1"))
	float DryingSeconds = 3600.0f;

	/** Game seconds of rate-1 precipitation to cover the ground in snow (where it is cold). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "1"))
	float SnowRiseSeconds = 1800.0f;

	/** Game seconds for deep snow to go. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "1"))
	float MeltSeconds = 14400.0f;

	// --- Shelter (the trace cone; a shelter provider answers first) ---

	/** Rays traced along the local up (one straight up, the rest in a cone) to find the open sky. 0 = no trace, exposure 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "0", ClampMax = "32"))
	int32 ShelterTraces = 5;

	/** How far up a ray looks, uu. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "100"))
	float ShelterDistance = 3000.0f;

	/** The cone's half angle for the outer rays, degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "0", ClampMax = "80"))
	float ShelterConeDegrees = 25.0f;

	/** Real seconds a requester's exposure is kept before it is traced again. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "0"))
	float ShelterInterval = 0.25f;

	/** The collision channel the rays test. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter")
	TEnumAsByte<ECollisionChannel> ShelterChannel = ECC_Visibility;

	// --- Tags ---

	/** Weather.Intensity.Heavy at or over this precipitation rate, Light under it. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0", ClampMax = "1"))
	float HeavyPrecipitationAbove = 0.5f;

	/** Weather.Fog at or over this fog density. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0", ClampMax = "1"))
	float FogTagAbove = 0.3f;

	/** Weather.Wind.Calm under this mean wind speed, uu/s. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0"))
	float CalmWindBelow = 150.0f;

	/** Weather.Wind.Strong at or over this mean wind speed, uu/s (Breeze between). */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0"))
	float StrongWindAbove = 600.0f;

	/** Weather.Sheltered under this exposure. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0", ClampMax = "1"))
	float ShelteredBelow = 0.5f;

	// --- Lightning ---

	/** How far from the local viewer a strike lands, uu (the place is per machine; the time is the clock minute's hash). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0"))
	float LightningRadius = 15000.0f;

	/** The rules the pure resolution uses, from the values above. */
	FWorldWeatherRules MakeRules() const;

	/** The accumulator rates, from the values above. */
	FWorldWeatherAccumulatorRates MakeRates() const;

	static const UWorldWeatherSettings* Get();
};
