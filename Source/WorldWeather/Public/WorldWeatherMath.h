// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldWeatherPreset.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherMath.generated.h"

/** The thresholds the pure resolution reads (UWorldWeatherSettings::MakeRules fills one from the settings). */
struct WORLDWEATHER_API FWorldWeatherRules
{
	float SnowBelowC = 0.0f;
	float SleetBandC = 2.0f;
	float DryPrecipitationScale = 0.25f;
	float HeavyPrecipitationAbove = 0.5f;
	float FogTagAbove = 0.3f;
	float CalmWindBelow = 150.0f;
	float StrongWindAbove = 600.0f;
	float ShelteredBelow = 0.5f;
};

/** How fast the ground wets, dries, whitens and melts (game seconds; UWorldWeatherSettings::MakeRates). */
struct WORLDWEATHER_API FWorldWeatherAccumulatorRates
{
	float WetnessRiseSeconds = 600.0f;
	float DryingSeconds = 3600.0f;
	float SnowRiseSeconds = 1800.0f;
	float MeltSeconds = 14400.0f;
};

/**
 * Pure weather arithmetic shared by the subsystem, tests and Blueprints: the blend, the schedule, the precipitation type,
 * the wind in the local frame, the accumulators, the lightning schedule, the resolution at a place. No state, no world.
 */
UCLASS()
class WORLDWEATHER_API UWorldWeatherMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- The blend ---

	/** An azimuth between two, the short way round; 0..360. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float LerpAzimuth(float FromDegrees, float ToDegrees, float Alpha);

	/** Member-by-member lerp of two fronts (the azimuth the short way). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static FWorldWeatherFront Blend(const FWorldWeatherFront& From, const FWorldWeatherFront& To, float Alpha);

	/** How far a blend has come at a clock time (0..1; 1 at once for a zero blend). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float BlendAlpha(double StartSeconds, float BlendSeconds, double NowSeconds);

	/** The present front from a sync point at a clock time. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static FWorldWeatherFront ResolveFront(const FWorldWeatherSyncPoint& Sync, double NowSeconds);

	/** The present preset from a sync point: the one left until the blend is half way, then the one arrived at. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static FGameplayTag ResolveTag(const FWorldWeatherSyncPoint& Sync, double NowSeconds);

	/** The blend and the hold have both run out: the schedule draws the next front. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static bool IsHoldOver(const FWorldWeatherSyncPoint& Sync, double NowSeconds);

	// --- The schedule ---

	/**
	 * The next front: a draw weighted by the presets' affinities at a climate, from a stream seeded by the world seed, the
	 * day and the draw's index, never the previous preset twice when another has weight.
	 * @param Seed The schedule seed (the settings' plus the provider's)
	 * @param Day The clock day of the draw
	 * @param Index How many fronts the schedule has drawn before this one
	 * @param Temperature The climate's temperature at the reference point (0..1)
	 * @param Moisture The climate's moisture there (0..1)
	 * @param Presets The table
	 * @param PreviousIndex The preset the world is at (-1 = none)
	 * @param OutHoldSeconds The hold drawn from the preset's range, game seconds
	 * @return The preset's index, -1 for an empty table or one with no weight anywhere
	 */
	static int32 PickNext(int32 Seed, int32 Day, int32 Index, float Temperature, float Moisture, const TArray<FWorldWeatherPreset>& Presets,
		int32 PreviousIndex, float& OutHoldSeconds);

	// --- Temperature and precipitation ---

	/**
	 * Degrees at a place: the climate's temperature mapped between the cold and hot ends, the night's cooling by how dark it
	 * is, the front's offset.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float TemperatureC(float Temperature01, float ColdC, float HotC, float Daylight, float NightCoolingC, float FrontOffsetC);

	/**
	 * The climate temperature (0..1) at which it snows: SnowBelowC inverted through the linear map (the materials compare a
	 * biome's mid temperature against it; the mirror source FreezeLevel). 0 when the map is degenerate.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float FreezeLevel(float ColdC, float HotC, float SnowBelowC);

	/** What falls: nothing at rate 0; snow at or under SnowBelowC, sleet within SleetBandC over it, rain above. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static EWorldWeatherPrecipitation PrecipitationType(float InTemperatureC, float Rate, float SnowBelowC, float SleetBandC);

	/** 1 at or under SnowBelowC, 0 at SnowBelowC + SleetBandC, linear between: how much of the cover is snow rather than wet. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float ColdFactor(float InTemperatureC, float SnowBelowC, float SleetBandC);

	/** The front's rate scaled by the moisture at the place (DryScale of it where the moisture is 0, all of it at 1). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float PrecipitationRateAt(float FrontRate, float Moisture, float DryScale);

	// --- Wind ---

	/**
	 * The local frame, cyclic with the world's axes (east x north = up, as X x Y = Z): north is the world +Z axis projected
	 * onto the tangent plane (toward the planet's pole), +Y where the up is +Z itself (a plane); east is north x up: the side
	 * of increasing longitude on a planet (where the sun rises, the clock's convention), +X on a plane (the clock's default
	 * sunrise bearing). So a plane's azimuth 0 is +Y and 90 is +X.
	 */
	static void LocalFrame(const FVector& Up, FVector& OutNorth, FVector& OutEast);

	/**
	 * The precipitation occlusion capture's frame about a view (the plan's D63): the capture sits OcclusionHeight above the
	 * view along the local up and looks down it, its texture's U along the capture's right (-east) and V along its down
	 * (-north), each over the capture's width (2 x extent). A pixel's capture coordinates: UV = 0.5 + ((P - Origin) . U,
	 * (P - Origin) . V); its depth along the capture: (P - Origin) . Forward. The precipitation material does the same sums
	 * from MPC_Weather's OcclusionOrigin / OcclusionU / OcclusionV / OcclusionForward.
	 * @param View The camera
	 * @param Up The local up
	 * @param Height How far above the view the capture sits
	 * @param Extent Half the capture's width
	 * @param OutOrigin The capture's position
	 * @param OutForward The capture's look direction (-Up)
	 * @param OutU The texture's U axis per world unit (the right / the width)
	 * @param OutV The texture's V axis per world unit (the down / the width)
	 * @param OutCameraUp The capture's up (north): the component's Z axis
	 */
	static void OcclusionFrame(const FVector& View, const FVector& Up, float Height, float Extent,
		FVector& OutOrigin, FVector& OutForward, FVector& OutU, FVector& OutV, FVector& OutCameraUp);

	/** A point's capture coordinates (UV in 0..1 inside the capture) and depth along the capture, from OcclusionFrame's. */
	static void OcclusionCoords(const FVector& Point, const FVector& Origin, const FVector& Forward, const FVector& U, const FVector& V,
		FVector2D& OutUV, float& OutDepth);

	/** The gust multiplier at a clock time: two slow sines; 1 for no gustiness, within 1 +- Gustiness. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static float GustFactor(double Seconds, float Gustiness);

	/** The wind at a place, world uu/s, tangent to Up, the gust folded in. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static FVector WindVector(float Speed, float AzimuthDegrees, const FVector& Up, float Gustiness, double Seconds);

	// --- The accumulators ---

	/**
	 * The ground's wetness and snow cover at a clock time, integrated forward from a sync point's values with the blending
	 * front's precipitation and humidity, in steps of a game minute (WorldWeatherConstants). The same inputs give the same
	 * result on every machine, so nothing is republished for them.
	 */
	static void IntegrateAccumulators(const FWorldWeatherSyncPoint& Sync, double NowSeconds, const FWorldWeatherAccumulatorRates& Rates,
		float& OutWetness, float& OutSnowCover);

	// --- Lightning ---

	/**
	 * Whether a clock minute has a strike and when in it: a hash of the seed and the minute against the rate (at most one a
	 * minute; a rate of 1 or more strikes every minute).
	 * @param Seed The schedule seed
	 * @param Minute The clock minute (clock seconds / 60)
	 * @param ThunderRate Strikes per game minute
	 * @param OutSecondInMinute The strike's second within the minute (0..60)
	 */
	static bool StrikeInMinute(int32 Seed, int64 Minute, float ThunderRate, float& OutSecondInMinute);

	// --- The resolution at a place ---

	/**
	 * What the front means at a place.
	 * @param Front The present (blended) front
	 * @param PresetTag The present preset
	 * @param Climate The climate and frame there
	 * @param InTemperatureC Degrees there (TemperatureC)
	 * @param Wetness The global accumulator
	 * @param SnowCover The global accumulator
	 * @param Exposure 1 under the sky, 0 sheltered
	 * @param Seconds Clock seconds (the gust phase)
	 * @param Rules The thresholds
	 */
	static FWorldWeatherConditions Resolve(const FWorldWeatherFront& Front, const FGameplayTag& PresetTag, const FWorldWeatherClimateSample& Climate,
		float InTemperatureC, float Wetness, float SnowCover, float Exposure, double Seconds, const FWorldWeatherRules& Rules);

	/** The Weather.Precipitation.* tag for a type. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Math")
	static FGameplayTag PrecipitationTag(EWorldWeatherPrecipitation Type);
};
