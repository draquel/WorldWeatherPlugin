// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherAmbienceComponent.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * What one local player hears of the weather (the plan's P4, D65), a component of AWorldWeatherViewer: three looping
 * two-dimensional sources, light rain, heavy rain and wind, named in the settings (any USoundBase; a MetaSound source
 * gets the float inputs Rate, Wind and Exposure as well), their volume from the conditions at the camera (the rain's
 * rate, crossfading light to heavy past half; the wind's intensity with the gusts), both quieter and low-passed by the
 * viewer's shelter (InteriorVolumeDrop, InteriorLowPassHz), and one-shot thunder queued by the lightning component after
 * the sound's travel time, quieter with distance. Started when a loop has volume, stopped with a fade when it has none.
 * Cvar ww.Viewer.Audio (0 silences it).
 */
UCLASS(ClassGroup = Weather, meta = (BlueprintSpawnableComponent))
class WORLDWEATHER_API UWorldWeatherAmbienceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWorldWeatherAmbienceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * The viewer's tick: the conditions at the camera and the frame's delta.
	 * @param Conditions What the weather is at the view
	 * @param WindIntensity The wind over the settings' strong wind, 0..1, with the gust factor
	 * @param DeltaTime Real seconds
	 */
	void UpdateWeather(const FWorldWeatherConditions& Conditions, float WindIntensity, float DeltaTime);

	/**
	 * A strike this far away: its thunder plays after Distance / SpeedOfSound, at a volume falling to 0 at ThunderMaxDistance.
	 * @param Distance From the viewer to the strike, uu
	 */
	void QueueThunder(float Distance);

	/** The light rain loop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAudioComponent> RainLight;

	/** The heavy rain loop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAudioComponent> RainHeavy;

	/** The wind loop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAudioComponent> Wind;

	/** The loops' volumes as last applied (light rain, heavy rain, wind), for the console. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FVector GetVolumes() const { return FVector(LastRainLight, LastRainHeavy, LastWind); }

	/** The low-pass cutoff as last applied, Hz. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetLowPassHz() const { return LastLowPassHz; }

	/** Thunder claps still to play, and played so far. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetPendingThunder() const { return PendingThunder.Num(); }

	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetThunderPlayed() const { return ThunderPlayed; }

	/** The MetaSound float inputs set on the loops when a source exposes them. */
	static const FName RateParameter;
	static const FName WindParameter;
	static const FName ExposureParameter;

protected:
	/** One loop's volume and filter: start it when it has volume, fade it out when it has none. */
	void DriveLoop(UAudioComponent* Loop, float Volume, float LowPassHz, float& LastVolume) const;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ThunderSound;

	struct FPendingThunder
	{
		double PlayAt = 0.0;
		float Volume = 1.0f;
	};
	TArray<FPendingThunder> PendingThunder;
	int32 ThunderPlayed = 0;

	float LastRainLight = 0.0f;
	float LastRainHeavy = 0.0f;
	float LastWind = 0.0f;
	float LastLowPassHz = 20000.0f;
};
