// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "WorldWeatherProviders.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherSubsystem.generated.h"

class ADayNightController;
class APlayerController;
class AWorldWeatherState;
class AWorldWeatherViewer;
class UCurveFloat;
class UMaterialParameterCollection;
class UWorldClockSubsystem;
class UWorldWeatherPresetTable;
enum class EWorldWeatherMirrorSource : uint8;

/** A mirror table entry with its collection loaded (UWorldWeatherSettings::MirrorTable, resolved at Initialize). */
USTRUCT()
struct FWorldWeatherMirrorTarget
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	FName Parameter;
	EWorldWeatherMirrorSource Source = EWorldWeatherMirrorSource(0);
	float Scale = 1.0f;
	float Offset = 0.0f;
};

/**
 * The weather (the plan's D58, D59). Game worlds only. The authority owns a timeline of fronts: it draws the next from the
 * schedule when a hold runs out (or is told one by SetWeather) and publishes one sync point through AWorldWeatherState;
 * every machine resolves the present front, the accumulators and the lightning from that sync point and the world clock's
 * synchronised seconds, so all read the same weather. GetConditionsAt resolves it at a place through the climate and
 * shelter providers a game sets (D60), or the defaults (a Z-up plane at the settings' temperature). Setters are
 * authority-only; the delegates fire on every machine.
 *
 * The sky (P1): every tick on every machine the present front fills the clock controller's modifier slot (D61: the sun,
 * the sky light, the height fog, the clouds, a lightning flash), MPC_Weather and the mirror table's collections (D64),
 * and a viewer actor per local player carries the local fog volume (D62).
 */
UCLASS()
class WORLDWEATHER_API UWorldWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Subsystem ---
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Convenience: the weather of an object's world (null outside game worlds). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather", meta = (WorldContext = "WorldContextObject"))
	static UWorldWeatherSubsystem* Get(const UObject* WorldContextObject);

	// --- Reading (every machine) ---

	/** The world clock's seconds since day 1 at 00:00 (the world's time without a clock): the timeline's time base. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	double GetClockSeconds() const;

	/** A sync point exists (the authority has drawn a front; a client has received one). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	bool HasWeather() const;

	/** The present front, blended (the defaults before the first sync point). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	FWorldWeatherFront GetCurrentFront() const;

	/** The present preset's tag (the one left until the blend is half way, then the one arrived at). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	FGameplayTag GetCurrentPreset() const;

	/** How far the present blend has come (0..1). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	float GetBlendAlpha() const;

	/** The ground's wetness now, the global accumulator (0 dry, 1 soaked). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	float GetWetness() const;

	/** The ground's snow cover now, the global accumulator (applied where it is cold). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	float GetSnowCover() const;

	/** The last sync point (the authority's own; a client's received one). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	FWorldWeatherSyncPoint GetSyncPoint() const;

	/** The schedule is stopped. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	bool IsLocked() const;

	/** The lightning flash on this machine now (1 at a strike, fading over the settings' LightningFlashSeconds). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	float GetLightningFlash() const;

	/**
	 * What the weather means at a place.
	 * @param WorldPos Position
	 * @param Requester The object asking (its exposure is cached for ShelterInterval; its actor is ignored by the trace); null traces every call
	 */
	UFUNCTION(BlueprintPure, Category = "WorldWeather", meta = (AdvancedDisplay = "Requester"))
	FWorldWeatherConditions GetConditionsAt(const FVector& WorldPos, const UObject* Requester = nullptr) const;

	/** 1 under the open sky, 0 fully sheltered: the shelter provider's answer, else the trace cone (cached per requester). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather", meta = (AdvancedDisplay = "Requester"))
	float GetExposureAt(const FVector& WorldPos, const UObject* Requester = nullptr) const;

	/** Degrees C at a place: the climate's temperature mapped by the settings, the night's cooling, the front's offset. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	float GetTemperatureAt(const FVector& WorldPos) const;

	/** The climate and frame at a place: the climate provider's, else the defaults. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	FWorldWeatherClimateSample GetClimateAt(const FVector& WorldPos) const;

	/** The fronts this world can have (the settings' table, or the defaults built in memory). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather")
	UWorldWeatherPresetTable* GetPresetTable() const { return Table; }

	/** The clock controller whose modifier slot the weather fills (found by class; null until one exists). */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Sky")
	ADayNightController* GetSkyController() const { return SkyController.Get(); }

	/** The viewer actors spawned for this machine's local players. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Sky")
	TArray<AWorldWeatherViewer*> GetViewers() const;

	/** Milliseconds the sky, collection and mirror writes took last tick, and their average over the last second. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Sky")
	void GetSkyWriteCost(float& OutLastMs, float& OutAverageMs) const { OutLastMs = SkyWriteLastMs; OutAverageMs = SkyWriteAverageMs; }

	/** The GPU frame time (the RHI's, what stat unit shows) averaged over the last second, ms: the A/B of the viewer's effects. */
	UFUNCTION(BlueprintPure, Category = "WorldWeather|Sky")
	float GetGPUFrameAverageMs() const { return GPUFrameAverageMs; }

	// --- Writing (authority) ---

	/**
	 * Blend to a preset now. Authority only; logs a warning elsewhere.
	 * @param PresetTag A row of the table
	 * @param BlendSeconds Game seconds for the blend; negative = the preset's own
	 * @return false when the tag is not a row or this is not the authority
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather")
	bool SetWeather(FGameplayTag PresetTag, float BlendSeconds = -1.0f);

	/** Turn the wind (degrees from north toward east, uu/s) over a short blend, keeping the front. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather")
	void SetWind(float AzimuthDegrees, float Speed);

	/** Draw the next front from the schedule now. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather")
	void SkipToNext();

	/** Stop or resume the schedule (the present front holds). Authority only. */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather")
	void SetLocked(bool bInLocked);

	/** A lightning strike at a place: fires OnLightning and the sky's flash on this machine (strike places are local; the time comes from the clock minute). */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather")
	void Strike(const FVector& Location);

	// --- Providers (a game-side bridge sets them; D60) ---

	void SetClimateProvider(const TScriptInterface<IWorldWeatherClimateProvider>& Provider);
	void SetShelterProvider(const TScriptInterface<IWorldWeatherShelterProvider>& Provider);
	const TScriptInterface<IWorldWeatherClimateProvider>& GetClimateProvider() const { return ClimateProvider; }
	const TScriptInterface<IWorldWeatherShelterProvider>& GetShelterProvider() const { return ShelterProvider; }

	// --- Save ---

	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Save")
	FWorldWeatherSaveState ExportState() const;

	/** Authority only. Re-primes the edges so a loaded storm fires no change event. */
	UFUNCTION(BlueprintCallable, Category = "WorldWeather|Save")
	void ImportState(const FWorldWeatherSaveState& InState);

	// --- Events (every machine) ---

	/** The present preset changed (half way through a blend). */
	UPROPERTY(BlueprintAssignable, Category = "WorldWeather|Events")
	FOnWorldWeatherChanged OnWeatherChanged;

	/** A lightning strike at a place near this machine's viewer. */
	UPROPERTY(BlueprintAssignable, Category = "WorldWeather|Events")
	FOnWorldWeatherLightning OnLightning;

protected:
	bool HasAuthority() const;
	UWorldClockSubsystem* FindClock() const;
	/** The sync point in force: the authority's own, a client's state's (null before the first). */
	const FWorldWeatherSyncPoint* FindSync() const;
	void Publish();
	/** The schedule seed: the settings' plus the climate provider's. */
	int32 GetScheduleSeed() const;
	/** Where the authority reads the climate for the schedule: the first player's position, else the origin. */
	FVector GetReferencePoint() const;
	/** Where this machine's first local player looks from (the camera, else the pawn), else the reference point. */
	FVector GetLocalViewPoint() const;
	/** Draw a front from the schedule and start the blend (no blend for the first). */
	bool DrawNext(double Now, bool bFirst);
	/** Start a blend from the present to a front. */
	void Transition(const FWorldWeatherFront& ToFront, const FGameplayTag& ToTag, float BlendSeconds, float HoldSeconds, double Now);
	void FireEdgeEvents(double Now);
	void TickLightning(double Now);
	/** The trace cone's exposure at a place. */
	float TraceExposure(const FVector& WorldPos, const FVector& Up, const UObject* Requester) const;

	// --- The sky (P1) ---

	/** Every tick: the flash's decay, the controller's slot, MPC_Weather, the mirror table, the viewers. */
	void TickSky(float DeltaTime, double Now);
	/** The clock controller's modifiers for a front and a flash (UWorldWeatherSettings' sky section). */
	static void FillModifiers(const FWorldWeatherFront& Front, float Flash, const class UWorldWeatherSettings& Settings, struct FDayNightModifiers& Out);
	/** One mirror source's value for a front at the local view. */
	float MirrorScalar(EWorldWeatherMirrorSource Source, const FWorldWeatherFront& Front, float Wetness, float SnowCover, float TemperatureC, float Gust) const;
	/** Spawn a viewer for each local player controller without one; drop those whose player is gone. */
	void UpdateViewers();

	/** Authority-owned timeline. */
	FWorldWeatherSyncPoint Sync;
	int32 ScheduleIndex = 0;
	int32 CurrentPresetIndex = -1;
	bool bLocked = false;
	bool bScheduleWarned = false;

	UPROPERTY()
	TObjectPtr<AWorldWeatherState> State;

	UPROPERTY()
	TObjectPtr<UWorldWeatherPresetTable> Table;

	UPROPERTY()
	TObjectPtr<UCurveFloat> TemperatureCurve;

	UPROPERTY()
	TScriptInterface<IWorldWeatherClimateProvider> ClimateProvider;

	UPROPERTY()
	TScriptInterface<IWorldWeatherShelterProvider> ShelterProvider;

	// Edge tracking (every machine).
	bool bEdgesPrimed = false;
	FGameplayTag LastPreset;
	int64 LastStruckMinute = -1;

	// The sky (every machine).
	TWeakObjectPtr<ADayNightController> SkyController;
	double NextControllerSearch = 0.0;
	float FlashRemaining = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> WeatherCollection;

	UPROPERTY()
	TArray<FWorldWeatherMirrorTarget> MirrorTargets;

	UPROPERTY()
	TArray<TObjectPtr<AWorldWeatherViewer>> Viewers;

	float SkyWriteLastMs = 0.0f;
	float SkyWriteAverageMs = 0.0f;
	double SkyWriteWindowStart = 0.0;
	double SkyWriteWindowSum = 0.0;
	int32 SkyWriteWindowCount = 0;
	double GPUFrameWindowSum = 0.0;
	float GPUFrameAverageMs = 0.0f;

	struct FExposureCacheEntry
	{
		double Time = 0.0;
		FVector Pos = FVector::ZeroVector;
		float Exposure = 1.0f;
	};
	mutable TMap<FObjectKey, FExposureCacheEntry> ExposureCache;
};
