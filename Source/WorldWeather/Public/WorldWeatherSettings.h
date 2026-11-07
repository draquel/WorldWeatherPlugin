// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherSettings.generated.h"

class UCurveFloat;
class UMaterialParameterCollection;
class UNiagaraSystem;
class UTextureRenderTarget2D;
class UWorldWeatherPresetTable;

/** What a mirror table entry writes into a collection's parameter (the plan's D64). */
UENUM(BlueprintType)
enum class EWorldWeatherMirrorSource : uint8
{
	/** The front's cloud cover, 0..1 */
	CloudCover,
	/** The front's cloud density, 0..1 */
	CloudDensity,
	/** The front's precipitation rate, 0..1 */
	Precipitation,
	/** The front's fog density, 0..1 */
	FogDensity,
	/** The mean wind speed, uu/s */
	WindSpeed,
	/** The mean wind speed over StrongWindAbove, 0..1 */
	WindIntensity,
	/** The gust multiplier this frame (GustFactor) */
	GustFactor,
	/** The front's gustiness, 0..1 */
	Gustiness,
	/** The front's thunder rate */
	ThunderRate,
	/** The front's humidity, 0..1 */
	Humidity,
	/** The global wetness accumulator, 0..1 */
	Wetness,
	/** The global snow cover accumulator, 0..1 */
	SnowCover,
	/** Degrees C at the local viewer */
	TemperatureC,
	/** The lightning flash this frame, 0..1 */
	LightningFlash,
	/** A vector: the wind direction at the local viewer (unit, tangent to the up) with the speed in W */
	WindDirection,
	/** The front's wind azimuth, degrees from north toward east (a pack's scalar wind direction; Scale and Offset turn it into the pack's convention) */
	WindAzimuth,
	/** The climate temperature (0..1) at which precipitation is snow (the settings' SnowBelowC through their map): the materials' snow line against a biome's temperature */
	FreezeLevel,
	/** The cold factor at the local viewer (1 at or under SnowBelowC, 0 a sleet band above it): the materials' snow where a biome's range says nothing */
	ColdFactor,
};

/** One parameter of one collection the weather writes every tick: Source x Scale + Offset (a vector source ignores both). */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherMirrorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
	TSoftObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
	FName Parameter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
	EWorldWeatherMirrorSource Source = EWorldWeatherMirrorSource::WindIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
	float Offset = 0.0f;
};

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

	/** Weather.Wind.Strong at or over this mean wind speed, uu/s (Breeze between). Also WindIntensity's full scale. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0"))
	float StrongWindAbove = 600.0f;

	/** Weather.Sheltered under this exposure. */
	UPROPERTY(Config, EditAnywhere, Category = "Tags", meta = (ClampMin = "0", ClampMax = "1"))
	float ShelteredBelow = 0.5f;

	// --- Lightning ---

	/** How far from the local viewer a strike lands, uu (the place is per machine; the time is the clock minute's hash). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0"))
	float LightningRadius = 15000.0f;

	/** Real seconds a strike's flash takes to fade from the sky (the clock controller's slot, MPC_Weather's LightningFlash). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0.01"))
	float LightningFlashSeconds = 0.35f;

	// --- The sky (the plan's D61: the clock controller's modifier slot, filled every tick on every machine) ---

	/** Fill the clock controller's modifier slot from the front (the sun, the sky light, the height fog, the clouds, the flash). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky")
	bool bDriveSky = true;

	/** How much a full cloud cover dims the sun (the sun's scale = 1 - cover x this - precipitation x SunDimByPrecipitation, at least SunScaleMin). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float SunDimByCloudCover = 0.6f;

	/** How much a full precipitation rate dims the sun further. */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float SunDimByPrecipitation = 0.2f;

	/** The least the sun is scaled to under any weather. */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float SunScaleMin = 0.15f;

	/** How much a full cloud cover dims the sky light. */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float SkyLightDimByCloudCover = 0.35f;

	/** The height fog's density scale at a full weather fog: 1 + fog x this (where a height fog exists and is shown). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0"))
	float FogDensityScaleByFog = 4.0f;

	/** The height fog's tint under a full cloud cover (white under a clear sky, blended by the cover). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky")
	FLinearColor FogTintOvercast = FLinearColor(0.72f, 0.75f, 0.8f);

	/**
	 * The clouds' coverage parameter at cloud cover 0 and 1. The engine's m_SimpleVolumetricCloud adds it to its weather map: its
	 * instance authors -0.2 (scattered cumulus); -0.35 is a few fair-weather clouds, 0.3 a closed ceiling.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "-1", ClampMax = "1"))
	float CloudCoverageMin = -0.35f;

	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "-1", ClampMax = "1"))
	float CloudCoverageMax = 0.3f;

	/** The clouds' density parameter at cloud density 0 and 1 (the engine instance authors 0.008; 0.06 is solid cumulus, 0.12 a dark ceiling). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudDensityMin = 0.006f;

	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudDensityMax = 0.12f;

	// --- Collections (the plan's D64) ---

	/** The weather's own collection, every parameter written each tick (MPC_Weather; the content script makes it). None: not written. */
	UPROPERTY(Config, EditAnywhere, Category = "Collections")
	TSoftObjectPtr<UMaterialParameterCollection> WeatherCollection;

	/** Other collections' parameters the weather fills (VoxelWorlds' wind inputs, a pack's own wind collection). */
	UPROPERTY(Config, EditAnywhere, Category = "Collections")
	TArray<FWorldWeatherMirrorEntry> MirrorTable;

	// --- The viewer (the plan's D62: a camera-following local fog volume per local player) ---

	/** Spawn an AWorldWeatherViewer for every local player controller (none on a dedicated server). */
	UPROPERTY(Config, EditAnywhere, Category = "Viewer")
	bool bAutoSpawnViewers = true;

	/** The local fog volume's radius about the camera, uu. */
	UPROPERTY(Config, EditAnywhere, Category = "Viewer", meta = (ClampMin = "500"))
	float LocalFogRadius = 30000.0f;

	/**
	 * The local fog's extinction per metre at a full weather fog, uniform across the sphere (the volume's height term; its
	 * radial term is only the edge): 0.02 is about 50 m of visibility, 0.005 a haze, 0 none.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Viewer", meta = (ClampMin = "0"))
	float LocalFogExtinctionMax = 0.02f;

	/** The local fog's albedo (its colour under light). */
	UPROPERTY(Config, EditAnywhere, Category = "Viewer")
	FLinearColor LocalFogAlbedo = FLinearColor(0.8f, 0.84f, 0.9f);

	/** The local fog's scattering distribution (0 even, toward 1 forward toward the sun). */
	UPROPERTY(Config, EditAnywhere, Category = "Viewer", meta = (ClampMin = "0", ClampMax = "0.99"))
	float LocalFogPhaseG = 0.3f;

	// --- The viewer's precipitation (the plan's P2, D63: a Niagara system per type about the camera, occluded by a top-down depth capture) ---

	/** The system for rain and sleet (the content script makes NS_WeatherRain from the pack's). None: no rain is drawn. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	/** The system for snow. None: no snow is drawn. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	/** The precipitation rate at the viewer under which the system is deactivated (its particles die out). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "0", ClampMax = "1"))
	float PrecipitationActivateAbove = 0.02f;

	/** How fast rain falls, uu/s: with the wind it sets the streaks' tilt (the system is rotated, not edited). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "1"))
	float RainFallSpeed = 1500.0f;

	/** How fast snow falls, uu/s: a slow fall drifts far with the wind. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "1"))
	float SnowFallSpeed = 300.0f;

	/**
	 * Capture the scene's depth from above the camera along the local up every OcclusionFrames frames into OcclusionTarget,
	 * so the precipitation material hides the particles under a roof, a canopy, a cave's ceiling (D63). Off: the particles
	 * fade with the viewer's traced exposure instead (all or nothing about the camera).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects")
	bool bOcclusionCapture = true;

	/**
	 * The render target the capture writes and the precipitation material samples (a tracked asset the content script makes,
	 * RT_WeatherOcclusion, one channel float). None: no capture.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects")
	TSoftObjectPtr<UTextureRenderTarget2D> OcclusionTarget;

	/** Half the capture's width about the camera, uu (the particles outside it are never occluded). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "1000"))
	float OcclusionExtent = 10000.0f;

	/** How far above the camera along the local up the capture looks down from, uu (a roof higher than this is missed). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "100"))
	float OcclusionHeight = 5000.0f;

	/** Frames between captures (the cvar ww.Occlusion.Frames overrides it). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "1", ClampMax = "60"))
	int32 OcclusionFrames = 4;

	/** A particle this far under the captured depth is still drawn (the capture's resolution and the sprites' size). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation Effects", meta = (ClampMin = "0"))
	float OcclusionBias = 40.0f;

	/** The rules the pure resolution uses, from the values above. */
	FWorldWeatherRules MakeRules() const;

	/** The accumulator rates, from the values above. */
	FWorldWeatherAccumulatorRates MakeRates() const;

	static const UWorldWeatherSettings* Get();
};
