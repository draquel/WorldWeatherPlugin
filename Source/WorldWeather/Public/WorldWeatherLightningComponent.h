// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldWeatherLightningComponent.generated.h"

class UNiagaraSystem;
class UWorldWeatherAmbienceComponent;

/**
 * A strike as one local player sees and hears it (the plan's P4), a component of AWorldWeatherViewer: bound to the
 * subsystem's OnLightning (the strike times are the clock minute's hash, the places this machine's), it spawns the
 * settings' LightningSystem at the strike's place when one is named (the flash itself is the sky's, through the clock
 * controller's modifier slot) and hands the thunder to the viewer's ambience, which plays it after the sound's travel
 * time. Nothing replicates: every machine strikes at the same minute at its own place.
 */
UCLASS(ClassGroup = Weather, meta = (BlueprintSpawnableComponent))
class WORLDWEATHER_API UWorldWeatherLightningComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWorldWeatherLightningComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The ambience the thunder goes to (the viewer's). */
	void SetAmbience(UWorldWeatherAmbienceComponent* InAmbience) { Ambience = InAmbience; }

	/** Strikes seen so far on this machine. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetStrikeCount() const { return StrikeCount; }

	/** Where the last strike landed. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FVector GetLastStrike() const { return LastStrike; }

protected:
	UFUNCTION()
	void OnStrike(FVector Location);

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> BoltSystem;

	TWeakObjectPtr<UWorldWeatherAmbienceComponent> Ambience;
	int32 StrikeCount = 0;
	FVector LastStrike = FVector::ZeroVector;
};
