// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldWeatherTypes.generated.h"

/** What falls at a place: decided from the temperature there, never authored on a front. */
UENUM(BlueprintType)
enum class EWorldWeatherPrecipitation : uint8
{
	None,
	Rain,
	Snow,
	Sleet,
};

/**
 * One weather: the values a preset authors and the timeline blends member by member (the azimuth the short way round).
 * Dimensionless 0..1 unless said otherwise.
 */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherFront
{
	GENERATED_BODY()

	/** How much of the sky the clouds cover (0 clear, 1 overcast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCover = 0.1f;

	/** How thick the clouds are (0 thin veils, 1 a dark ceiling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudDensity = 0.3f;

	/** Precipitation rate (0 none, 1 a downpour). The type (rain, snow, sleet) is decided at the position by the temperature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Precipitation = 0.0f;

	/** Weather fog over the sky's own (0 none, 1 a whiteout). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogDensity = 0.0f;

	/** Mean wind speed, uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float WindSpeed = 150.0f;

	/**
	 * Where the wind blows toward: degrees from the local frame's north toward its east, where the sun rises (on a plane
	 * north is +Y and east +X, the clock's default sunrise bearing; on a planet north is toward the +Z pole and east the
	 * side of increasing longitude). UWorldWeatherMath::LocalFrame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "360"))
	float WindAzimuth = 0.0f;

	/** How much the wind gusts about its mean (0 steady, 1 up to double and down to nothing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Gustiness = 0.2f;

	/** Lightning strikes per game minute (0 none; 1 one every minute). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float ThunderRate = 0.0f;

	/** Added to the temperature everywhere, degrees C (a cold front is negative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float TemperatureOffset = 0.0f;

	/** How damp the air is: the wetness accumulation rate and the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Humidity = 0.3f;
};

/** What a climate provider knows about a place. Without a provider the subsystem uses the defaults here (a Z-up plane). */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherClimateSample
{
	GENERATED_BODY()

	/** Dimensionless temperature, 0 the coldest the world has, 1 the hottest (the settings map it to degrees). */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	float Temperature = 0.5f;

	/** Dimensionless moisture, 0 the driest, 1 the wettest. */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	float Moisture = 0.5f;

	/** Height over the sea datum, uu. */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	float Altitude = 0.0f;

	/** The local up (the planet's radial direction, +Z on a plane). */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	FVector Up = FVector::UpVector;

	/** The position is under the water. */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	bool bUnderwater = false;
};

/**
 * The replicated sync point (the fields of AWorldWeatherState): the front the world blends from and to, when the blend
 * started in clock time, how long it takes and holds, and the accumulators at its start. Every machine resolves the present
 * from this and the world clock, so a late joiner is right on its first frame and a paused clock pauses the weather.
 */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherSyncPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FWorldWeatherFront FromFront;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FWorldWeatherFront ToFront;

	/** The preset the world is leaving. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FGameplayTag FromTag;

	/** The preset the world is arriving at (the present tag once the blend is half way). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FGameplayTag ToTag;

	/** Clock seconds (day 1 at 00:00 = 0) when the blend started. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	double StartSeconds = 0.0;

	/** Game seconds the blend takes (0 = at once). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float BlendSeconds = 0.0f;

	/** Game seconds the arrived front holds after the blend before the schedule draws the next. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float HoldSeconds = 0.0f;

	/** The ground's wetness at StartSeconds (0 dry, 1 soaked). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Wetness = 0.0f;

	/** The ground's snow cover at StartSeconds (0 none, 1 deep), applied where it is cold. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float SnowCover = 0.0f;

	/** Counts publishes; 0 = none yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	int32 Serial = 0;

	bool IsValid() const { return Serial > 0; }
};

/** What a consumer gets at a position: the resolved front and what it means there. */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherConditions
{
	GENERATED_BODY()

	/** The blended front (global; the members below are the local reading of it). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FWorldWeatherFront Front;

	/** The preset the world is at (From until the blend is half way, then To). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FGameplayTag PresetTag;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	EWorldWeatherPrecipitation PrecipitationType = EWorldWeatherPrecipitation::None;

	/** The front's rate times the moisture factor at the place (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float PrecipitationRate = 0.0f;

	/** Wind, world uu/s, tangent to Up, the gust phase folded in. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FVector WindVector = FVector::ZeroVector;

	/** The local up at the place. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FVector Up = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float TemperatureC = 15.0f;

	/** The ground's wetness here (the global accumulator, dry where it is cold enough to snow). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Wetness = 0.0f;

	/** The snow cover here (the global accumulator where it is cold, none where it is warm). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float SnowCover = 0.0f;

	/** 1 under the open sky, 0 fully sheltered. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Exposure = 1.0f;

	/** Weather.* tags that hold here: the preset, the precipitation type and intensity, storm, fog, the wind band, sheltered. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FGameplayTagContainer Tags;
};

/** Everything a save system needs to restore the weather (the sync point, the schedule's place, the overrides). */
USTRUCT(BlueprintType)
struct WORLDWEATHER_API FWorldWeatherSaveState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	FWorldWeatherSyncPoint Sync;

	/** How many fronts the schedule has drawn. */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	int32 ScheduleIndex = 0;

	/** The schedule is stopped (a console command or a quest holds the weather). */
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	bool bLocked = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWorldWeatherChanged, FGameplayTag, OldPreset, FGameplayTag, NewPreset);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldWeatherLightning, FVector, Location);

namespace WorldWeatherConstants
{
	/** The accumulators integrate in steps of one game minute (the step grows past 256 minutes so a locked front stays cheap). */
	constexpr double AccumulatorStepSeconds = 60.0;
	constexpr int32 AccumulatorMaxSteps = 256;
}
