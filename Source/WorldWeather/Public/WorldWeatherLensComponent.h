// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherLensComponent.generated.h"

class UMaterialInstanceDynamic;
class UPostProcessComponent;

/**
 * The weather on one local player's lens (the plan's P4), a component of AWorldWeatherViewer: an unbound post-process
 * component carrying a dynamic instance of the settings' LensMaterial (M_WeatherLens) as its one blendable. The viewer
 * feeds it the conditions at the camera and the camera's look: the instance's Drops is the precipitation rate times
 * the exposure times how far up the camera looks, Frost the cold factor at the view, Wet the ground's wetness; the
 * post-process weight blends toward the strongest of them (FInterpTo at LensBlendSpeed) and is 0 when nothing shows,
 * so a clear day costs no pass. Local players only: the viewer exists per local player controller. Cvar ww.Viewer.Lens.
 */
UCLASS(ClassGroup = Weather, meta = (BlueprintSpawnableComponent))
class WORLDWEATHER_API UWorldWeatherLensComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWorldWeatherLensComponent();

	virtual void BeginPlay() override;

	/**
	 * The viewer's tick.
	 * @param Conditions What the weather is at the view
	 * @param ColdFactor The cold at the view (1 at or under the snow temperature)
	 * @param LookUp How far up the camera looks: the camera forward against the local up, -1..1
	 * @param DeltaTime Real seconds
	 */
	void UpdateWeather(const FWorldWeatherConditions& Conditions, float ColdFactor, float LookUp, float DeltaTime);

	/** The post-process volume (unbound). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** The blend weight as last applied. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetWeight() const { return Weight; }

	/** The instance's inputs as last applied (Drops, Frost, Wet). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FVector GetInputs() const { return FVector(LastDrops, LastFrost, LastWet); }

	/** The material parameter names. */
	static const FName DropsParameter;
	static const FName FrostParameter;
	static const FName WetParameter;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Instance;

	float Weight = 0.0f;
	float LastDrops = 0.0f;
	float LastFrost = 0.0f;
	float LastWet = 0.0f;
};
