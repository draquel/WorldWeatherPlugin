// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherPrecipitationComponent.generated.h"

class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * The precipitation one local player sees (the plan's P2, D63), a component of AWorldWeatherViewer: a Niagara system
 * per type (the settings' RainSystem for rain and sleet, SnowSystem for snow) about the camera, rotated so its fall
 * leans with the wind, active while the rate at the viewer is over the settings' threshold; and an orthographic depth
 * capture looking down the local up from above the camera every few frames into the settings' render target, whose
 * frame goes into MPC_Weather (OcclusionOrigin, OcclusionU, OcclusionV, OcclusionForward) so the precipitation material
 * hides every particle under the captured depth: dry under a roof, a canopy, a cave's ceiling, with no geometry
 * knowledge. The systems' user parameters User.Rate (float), User.Wind and User.Up (vectors) are set when they exist.
 * Cvars: ww.Viewer.Precipitation (the particles), ww.Viewer.Occlusion (the capture), ww.Occlusion.Frames (the interval).
 */
UCLASS(ClassGroup = Weather, meta = (BlueprintSpawnableComponent))
class WORLDWEATHER_API UWorldWeatherPrecipitationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWorldWeatherPrecipitationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * The viewer's tick: the conditions at the camera, the camera's position, the frame's delta.
	 * @param Conditions What the weather is at the view (the type, the rate, the wind, the up, the exposure)
	 * @param View The camera's position (the owner has moved there already)
	 * @param DeltaTime Real seconds
	 */
	void UpdateWeather(const FWorldWeatherConditions& Conditions, const FVector& View, float DeltaTime);

	/** The particles. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UNiagaraComponent> Particles;

	/** The occlusion capture (orthographic, scene depth, into the settings' OcclusionTarget). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<USceneCaptureComponent2D> Occlusion;

	/** The type drawn now (None while inactive). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	EWorldWeatherPrecipitation GetActiveType() const { return ActiveType; }

	/** The rate written to the material this tick (the rate at the view, times the exposure when the capture is off). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetActiveRate() const { return ActiveRate; }

	/** The capture is set up and running (a target, the setting and the cvar on, the particles active). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	bool IsOcclusionCapturing() const { return bCapturing; }

	/** How many captures have been made. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetCaptureCount() const { return CaptureCount; }

	/** The particles' user parameter names, set when the system exposes them. */
	static const FName RateParameter;
	static const FName WindParameter;
	static const FName UpParameter;

protected:
	/** The system for a type (the settings'), or null. */
	UNiagaraSystem* SystemFor(EWorldWeatherPrecipitation Type) const;
	/** Set a user parameter when the active system exposes it. */
	void SetUserFloat(const FName& Name, float Value) const;
	void SetUserVector(const FName& Name, const FVector& Value) const;
	/** The capture's transform, the collection's frame and a capture when its frame is due. */
	void UpdateOcclusion(const FVector& View, const FVector& Up, bool bActive);
	/** The collection's occlusion frame with the capture off (the material then skips the compare). */
	void WriteOcclusionOff() const;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> SnowSystem;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> OcclusionTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	EWorldWeatherPrecipitation ActiveType = EWorldWeatherPrecipitation::None;
	float ActiveRate = 0.0f;
	bool bCapturing = false;
	int32 CaptureCount = 0;
	uint64 LastCaptureFrame = 0;
};
