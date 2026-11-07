// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldWeatherViewer.generated.h"

class APlayerController;
class ULocalFogVolumeComponent;

/**
 * The weather as one local player sees it: an actor the subsystem spawns per local player controller
 * (UWorldWeatherSettings::bAutoSpawnViewers) that follows the player's camera every tick and carries the effects that
 * are generic to any world. P1: a local fog volume (the plan's D62), the weather's fog everywhere, on a planet too where
 * the clock controller hides the height fog. Later phases add precipitation, ambience, the lens and lightning. Not
 * replicated; never on a dedicated server.
 */
UCLASS(NotBlueprintable, Transient)
class WORLDWEATHER_API AWorldWeatherViewer : public AActor
{
	GENERATED_BODY()

public:
	AWorldWeatherViewer();

	virtual void Tick(float DeltaSeconds) override;

	/** The player this viewer follows (its camera manager's location, or its pawn's). */
	void SetPlayer(APlayerController* InPlayer);
	APlayerController* GetPlayer() const { return Player.Get(); }

	/** The weather fog about the camera (ULocalFogVolumeComponent; radius UWorldWeatherSettings::LocalFogRadius). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<ULocalFogVolumeComponent> LocalFog;

	/** The fog's radial density as last applied (0 = none). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetLocalFogExtinction() const { return LastFogExtinction; }

protected:
	/** Where the player looks from: the camera manager's location, else the pawn's. */
	bool GetViewLocation(FVector& OutLocation) const;

	TWeakObjectPtr<APlayerController> Player;
	float LastFogExtinction = 0.0f;
};
