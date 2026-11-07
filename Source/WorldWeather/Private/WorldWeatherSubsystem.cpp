// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherSubsystem.h"
#include "WorldWeather.h"
#include "WorldWeatherMath.h"
#include "DynamicRHI.h"
#include "WorldWeatherPreset.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherState.h"
#include "WorldWeatherViewer.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Curves/CurveFloat.h"
#include "DayNightController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CoreMisc.h"
#include "WorldClockSubsystem.h"
#include "WorldClockTypes.h"

bool UWorldWeatherSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UWorldWeatherSubsystem* UWorldWeatherSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UWorldWeatherSubsystem>() : nullptr;
}

void UWorldWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The clock first: the timeline reads its seconds.
	Collection.InitializeDependency<UWorldClockSubsystem>();

	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	// A named asset that is not there (the content script not run yet, a cooked build without it) is quietly the default:
	// the package is looked for before the load, which would log a warning.
	auto LoadIfPresent = [](const FSoftObjectPath& Path) -> UObject*
	{
		return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName()) ? Path.TryLoad() : nullptr;
	};
	Table = Cast<UWorldWeatherPresetTable>(LoadIfPresent(Settings->PresetTable.ToSoftObjectPath()));
	if (!Table || Table->Presets.Num() == 0)
	{
		Table = NewObject<UWorldWeatherPresetTable>(this, TEXT("DefaultWeatherPresets"));
		Table->ResetToDefaults();
	}
	TemperatureCurve = Cast<UCurveFloat>(LoadIfPresent(Settings->TemperatureCurve.ToSoftObjectPath()));
	WeatherCollection = Cast<UMaterialParameterCollection>(LoadIfPresent(Settings->WeatherCollection.ToSoftObjectPath()));
	for (const FWorldWeatherMirrorEntry& Entry : Settings->MirrorTable)
	{
		const FSoftObjectPath CollectionPath = Entry.Collection.ToSoftObjectPath();
		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(LoadIfPresent(CollectionPath));
		if (!MPC || Entry.Parameter.IsNone())
		{
			if (!Entry.Parameter.IsNone() && CollectionPath.IsValid() && !FPackageName::DoesPackageExist(CollectionPath.GetLongPackageName()))
			{
				// A pack's collection that is not installed in this checkout (the Spruce pack's MPC_Global): quietly nothing.
				UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: the mirror entry %s / %s names a collection that is not installed; skipped."),
					*Entry.Collection.ToString(), *Entry.Parameter.ToString());
				continue;
			}
			UE_LOG(LogWorldWeather, Warning, TEXT("WorldWeather: the mirror entry %s / %s names no collection or no parameter; skipped."),
				*Entry.Collection.ToString(), *Entry.Parameter.ToString());
			continue;
		}
		FWorldWeatherMirrorTarget& Target = MirrorTargets.AddDefaulted_GetRef();
		Target.Collection = MPC;
		Target.Parameter = Entry.Parameter;
		Target.Source = Entry.Source;
		Target.Scale = Entry.Scale;
		Target.Offset = Entry.Offset;
	}
}

void UWorldWeatherSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (HasAuthority())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		State = InWorld.SpawnActor<AWorldWeatherState>(AWorldWeatherState::StaticClass(), FTransform::Identity, Params);
		if (Sync.IsValid())
		{
			Publish(); // a front set before play (ImportState, a test) reaches the clients
		}
	}
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: %s starts with %d presets, %s, %d mirror targets (%s)."), *InWorld.GetName(), Table ? Table->Presets.Num() : 0,
		WeatherCollection ? *WeatherCollection->GetName() : TEXT("no weather collection"), MirrorTargets.Num(), HasAuthority() ? TEXT("authority") : TEXT("client"));
}

void UWorldWeatherSubsystem::Deinitialize()
{
	State = nullptr;
	ClimateProvider = nullptr;
	ShelterProvider = nullptr;
	Viewers.Reset();
	Super::Deinitialize();
}

bool UWorldWeatherSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->HasBegunPlay();
}

TStatId UWorldWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldWeatherSubsystem, STATGROUP_Tickables);
}

void UWorldWeatherSubsystem::Tick(float DeltaTime)
{
	const double Now = GetClockSeconds();
	if (HasAuthority())
	{
		if (!Sync.IsValid())
		{
			DrawNext(Now, /*bFirst=*/true);
		}
		else if (!bLocked && UWorldWeatherSettings::Get()->bAutoSchedule && UWorldWeatherMath::IsHoldOver(Sync, Now))
		{
			DrawNext(Now, /*bFirst=*/false);
		}
	}
	FireEdgeEvents(Now);
	TickLightning(Now);
	TickSky(DeltaTime, Now);
}

bool UWorldWeatherSubsystem::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

UWorldClockSubsystem* UWorldWeatherSubsystem::FindClock() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UWorldClockSubsystem>() : nullptr;
}

double UWorldWeatherSubsystem::GetClockSeconds() const
{
	if (const UWorldClockSubsystem* Clock = FindClock())
	{
		return static_cast<double>(Clock->GetDay() - 1) * WorldClockConstants::SecondsPerDay + Clock->GetTimeOfDaySeconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

const FWorldWeatherSyncPoint* UWorldWeatherSubsystem::FindSync() const
{
	if (HasAuthority())
	{
		return Sync.IsValid() ? &Sync : nullptr;
	}
	if (!State)
	{
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<AWorldWeatherState> It(World); It; ++It)
			{
				const_cast<UWorldWeatherSubsystem*>(this)->State = *It;
				break;
			}
		}
	}
	return State && State->HasSync() ? &State->GetSync() : nullptr;
}

bool UWorldWeatherSubsystem::HasWeather() const
{
	return FindSync() != nullptr;
}

FWorldWeatherFront UWorldWeatherSubsystem::GetCurrentFront() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	return S ? UWorldWeatherMath::ResolveFront(*S, GetClockSeconds()) : FWorldWeatherFront();
}

FGameplayTag UWorldWeatherSubsystem::GetCurrentPreset() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	return S ? UWorldWeatherMath::ResolveTag(*S, GetClockSeconds()) : FGameplayTag();
}

float UWorldWeatherSubsystem::GetBlendAlpha() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	return S ? UWorldWeatherMath::BlendAlpha(S->StartSeconds, S->BlendSeconds, GetClockSeconds()) : 1.0f;
}

float UWorldWeatherSubsystem::GetWetness() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	float W = 0.0f, Snow = 0.0f;
	if (S)
	{
		UWorldWeatherMath::IntegrateAccumulators(*S, GetClockSeconds(), UWorldWeatherSettings::Get()->MakeRates(), W, Snow);
	}
	return W;
}

float UWorldWeatherSubsystem::GetSnowCover() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	float W = 0.0f, Snow = 0.0f;
	if (S)
	{
		UWorldWeatherMath::IntegrateAccumulators(*S, GetClockSeconds(), UWorldWeatherSettings::Get()->MakeRates(), W, Snow);
	}
	return Snow;
}

FWorldWeatherSyncPoint UWorldWeatherSubsystem::GetSyncPoint() const
{
	const FWorldWeatherSyncPoint* S = FindSync();
	return S ? *S : FWorldWeatherSyncPoint();
}

bool UWorldWeatherSubsystem::IsLocked() const
{
	if (HasAuthority())
	{
		return bLocked;
	}
	FindSync();
	return State && State->IsLocked();
}

float UWorldWeatherSubsystem::GetLightningFlash() const
{
	const float Seconds = FMath::Max(UWorldWeatherSettings::Get()->LightningFlashSeconds, 0.01f);
	return FMath::Clamp(FlashRemaining / Seconds, 0.0f, 1.0f);
}

FWorldWeatherClimateSample UWorldWeatherSubsystem::GetClimateAt(const FVector& WorldPos) const
{
	FWorldWeatherClimateSample Sample;
	if (ClimateProvider && ClimateProvider->GetClimateAt(WorldPos, Sample))
	{
		Sample.Up = Sample.Up.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
		return Sample;
	}
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	Sample.Temperature = Settings->DefaultTemperature;
	Sample.Moisture = Settings->DefaultMoisture;
	Sample.Altitude = static_cast<float>(WorldPos.Z);
	Sample.Up = FVector::UpVector;
	Sample.bUnderwater = false;
	return Sample;
}

float UWorldWeatherSubsystem::GetTemperatureAt(const FVector& WorldPos) const
{
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	const FWorldWeatherClimateSample Climate = GetClimateAt(WorldPos);
	const UWorldClockSubsystem* Clock = FindClock();
	const float Daylight = Clock ? Clock->GetLocalDaylight(WorldPos) : 1.0f;
	const FWorldWeatherFront Front = GetCurrentFront();
	if (TemperatureCurve)
	{
		// The curve in place of the linear map; the night's cooling and the front's offset as before.
		return TemperatureCurve->GetFloatValue(FMath::Clamp(Climate.Temperature, 0.0f, 1.0f)) - Settings->NightCoolingC * (1.0f - FMath::Clamp(Daylight, 0.0f, 1.0f))
			+ Front.TemperatureOffset;
	}
	return UWorldWeatherMath::TemperatureC(Climate.Temperature, Settings->ColdTemperatureC, Settings->HotTemperatureC, Daylight, Settings->NightCoolingC, Front.TemperatureOffset);
}

float UWorldWeatherSubsystem::GetExposureAt(const FVector& WorldPos, const UObject* Requester) const
{
	const FWorldWeatherClimateSample Climate = GetClimateAt(WorldPos);
	if (Climate.bUnderwater)
	{
		return 0.0f;
	}
	float Exposure = 1.0f;
	if (ShelterProvider && ShelterProvider->GetExposureAt(WorldPos, Climate.Up, Exposure))
	{
		return FMath::Clamp(Exposure, 0.0f, 1.0f);
	}
	return TraceExposure(WorldPos, Climate.Up, Requester);
}

float UWorldWeatherSubsystem::TraceExposure(const FVector& WorldPos, const FVector& Up, const UObject* Requester) const
{
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	UWorld* World = GetWorld();
	if (!World || Settings->ShelterTraces <= 0)
	{
		return 1.0f;
	}
	const double Now = World->GetTimeSeconds();
	FObjectKey Key;
	if (Requester)
	{
		Key = FObjectKey(Requester);
		if (const FExposureCacheEntry* Cached = ExposureCache.Find(Key))
		{
			if (Now - Cached->Time < Settings->ShelterInterval && FVector::DistSquared(Cached->Pos, WorldPos) < 100.0 * 100.0)
			{
				return Cached->Exposure;
			}
		}
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(WorldWeatherShelter), /*bTraceComplex=*/false);
	if (const AActor* Actor = Cast<AActor>(Requester))
	{
		Params.AddIgnoredActor(Actor);
	}
	else if (const UActorComponent* Component = Cast<UActorComponent>(Requester))
	{
		if (const AActor* Owner = Component->GetOwner())
		{
			Params.AddIgnoredActor(Owner);
		}
	}
	FVector North, East;
	UWorldWeatherMath::LocalFrame(Up, North, East);
	const float Cone = FMath::DegreesToRadians(Settings->ShelterConeDegrees);
	const int32 N = Settings->ShelterTraces;
	int32 Open = 0;
	for (int32 i = 0; i < N; ++i)
	{
		FVector Dir = Up;
		if (i > 0)
		{
			// The outer rays around the cone, evenly spaced.
			const float Azimuth = UE_TWO_PI * static_cast<float>(i - 1) / static_cast<float>(FMath::Max(N - 1, 1));
			Dir = (Up * FMath::Cos(Cone) + (North * FMath::Cos(Azimuth) + East * FMath::Sin(Azimuth)) * FMath::Sin(Cone)).GetSafeNormal();
		}
		if (!World->LineTraceTestByChannel(WorldPos, WorldPos + Dir * Settings->ShelterDistance, Settings->ShelterChannel, Params))
		{
			++Open;
		}
	}
	const float Exposure = static_cast<float>(Open) / static_cast<float>(N);
	if (Requester)
	{
		FExposureCacheEntry& Entry = ExposureCache.FindOrAdd(Key);
		Entry.Time = Now;
		Entry.Pos = WorldPos;
		Entry.Exposure = Exposure;
	}
	return Exposure;
}

FWorldWeatherConditions UWorldWeatherSubsystem::GetConditionsAt(const FVector& WorldPos, const UObject* Requester) const
{
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	const double Now = GetClockSeconds();
	const FWorldWeatherSyncPoint* S = FindSync();
	const FWorldWeatherFront Front = S ? UWorldWeatherMath::ResolveFront(*S, Now) : FWorldWeatherFront();
	const FGameplayTag Preset = S ? UWorldWeatherMath::ResolveTag(*S, Now) : FGameplayTag();
	float Wetness = 0.0f, SnowCover = 0.0f;
	if (S)
	{
		UWorldWeatherMath::IntegrateAccumulators(*S, Now, Settings->MakeRates(), Wetness, SnowCover);
	}
	const FWorldWeatherClimateSample Climate = GetClimateAt(WorldPos);
	return UWorldWeatherMath::Resolve(Front, Preset, Climate, GetTemperatureAt(WorldPos), Wetness, SnowCover, GetExposureAt(WorldPos, Requester), Now, Settings->MakeRules());
}

// --- Writing ---

void UWorldWeatherSubsystem::Publish()
{
	if (State && HasAuthority())
	{
		State->Publish(Sync, bLocked);
	}
}

int32 UWorldWeatherSubsystem::GetScheduleSeed() const
{
	return UWorldWeatherSettings::Get()->ScheduleSeed + (ClimateProvider ? ClimateProvider->GetWeatherSeed() : 0);
}

FVector UWorldWeatherSubsystem::GetReferencePoint() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC)
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
		FVector Loc;
		FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		return Loc;
	}
	return FVector::ZeroVector;
}

FVector UWorldWeatherSubsystem::GetLocalViewPoint() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World && GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (PC)
	{
		if (PC->PlayerCameraManager)
		{
			return PC->PlayerCameraManager->GetCameraLocation();
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}
	return GetReferencePoint();
}

bool UWorldWeatherSubsystem::DrawNext(double Now, bool bFirst)
{
	if (!Table || Table->Presets.Num() == 0)
	{
		return false;
	}
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	// The first front: the settings' start preset when it names a row.
	if (bFirst && Settings->StartPreset.IsValid())
	{
		if (const FWorldWeatherPreset* Start = Table->FindByTag(Settings->StartPreset))
		{
			Transition(Start->Front, Start->Tag, 0.0f, FMath::Max(Start->HoldMinutesMin, Start->HoldMinutesMax) * 60.0f, Now);
			return true;
		}
		UE_LOG(LogWorldWeather, Warning, TEXT("WorldWeather: the start preset %s is not a row of the table; the schedule draws the first front."), *Settings->StartPreset.ToString());
	}
	const FVector Reference = GetReferencePoint();
	const FWorldWeatherClimateSample Climate = GetClimateAt(Reference);
	const UWorldClockSubsystem* Clock = FindClock();
	const int32 Day = Clock ? Clock->GetDay() : 1;
	float HoldSeconds = 0.0f;
	const int32 Picked = UWorldWeatherMath::PickNext(GetScheduleSeed(), Day, ScheduleIndex, Climate.Temperature, Climate.Moisture, Table->Presets, CurrentPresetIndex, HoldSeconds);
	if (Picked < 0)
	{
		if (!bScheduleWarned)
		{
			UE_LOG(LogWorldWeather, Warning, TEXT("WorldWeather: no preset has weight at the climate (T %.2f, M %.2f); the weather holds."), Climate.Temperature, Climate.Moisture);
			bScheduleWarned = true;
		}
		return false;
	}
	++ScheduleIndex;
	const FWorldWeatherPreset& P = Table->Presets[Picked];
	Transition(P.Front, P.Tag, bFirst ? 0.0f : P.BlendMinutes * 60.0f, HoldSeconds, Now);
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: draw %d on day %d at %s (T %.2f, M %.2f): %s, blend %.0f s, hold %.0f s."), ScheduleIndex, Day,
		*Reference.ToCompactString(), Climate.Temperature, Climate.Moisture, *P.Tag.ToString(), Sync.BlendSeconds, Sync.HoldSeconds);
	return true;
}

void UWorldWeatherSubsystem::Transition(const FWorldWeatherFront& ToFront, const FGameplayTag& ToTag, float BlendSeconds, float HoldSeconds, double Now)
{
	FWorldWeatherSyncPoint Next;
	if (Sync.IsValid())
	{
		Next.FromFront = UWorldWeatherMath::ResolveFront(Sync, Now);
		Next.FromTag = UWorldWeatherMath::ResolveTag(Sync, Now);
		UWorldWeatherMath::IntegrateAccumulators(Sync, Now, UWorldWeatherSettings::Get()->MakeRates(), Next.Wetness, Next.SnowCover);
	}
	else
	{
		Next.FromFront = ToFront;
		Next.FromTag = ToTag;
	}
	Next.ToFront = ToFront;
	Next.ToTag = ToTag;
	Next.StartSeconds = Now;
	Next.BlendSeconds = FMath::Max(BlendSeconds, 0.0f);
	Next.HoldSeconds = FMath::Max(HoldSeconds, 0.0f);
	Next.Serial = Sync.Serial + 1;
	Sync = Next;
	CurrentPresetIndex = Table ? Table->IndexOfTag(ToTag) : -1;
	Publish();
}

bool UWorldWeatherSubsystem::SetWeather(FGameplayTag PresetTag, float BlendSeconds)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("SetWeather(%s) called on a client — ignored."), *PresetTag.ToString());
		return false;
	}
	const FWorldWeatherPreset* P = Table ? Table->FindByTag(PresetTag) : nullptr;
	if (!P)
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("SetWeather: %s is not a row of the preset table."), *PresetTag.ToString());
		return false;
	}
	const double Now = GetClockSeconds();
	Transition(P->Front, P->Tag, BlendSeconds < 0.0f ? P->BlendMinutes * 60.0f : BlendSeconds, FMath::Max(P->HoldMinutesMin, P->HoldMinutesMax) * 60.0f, Now);
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: set to %s, blend %.0f s, hold %.0f s."), *P->Tag.ToString(), Sync.BlendSeconds, Sync.HoldSeconds);
	return true;
}

void UWorldWeatherSubsystem::SetWind(float AzimuthDegrees, float Speed)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("SetWind called on a client — ignored."));
		return;
	}
	const double Now = GetClockSeconds();
	FWorldWeatherFront Front = Sync.IsValid() ? Sync.ToFront : GetCurrentFront();
	Front.WindAzimuth = FMath::Fmod(AzimuthDegrees + 720.0f, 360.0f);
	Front.WindSpeed = FMath::Max(Speed, 0.0f);
	// The remaining hold, so the schedule's next draw keeps its time.
	const float Remaining = Sync.IsValid() ? static_cast<float>(FMath::Max(Sync.StartSeconds + Sync.BlendSeconds + Sync.HoldSeconds - Now, 0.0)) : 0.0f;
	Transition(Front, Sync.IsValid() ? Sync.ToTag : GetCurrentPreset(), 30.0f, Remaining, Now);
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: wind to %.0f deg at %.0f uu/s."), Front.WindAzimuth, Front.WindSpeed);
}

void UWorldWeatherSubsystem::SkipToNext()
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("SkipToNext called on a client — ignored."));
		return;
	}
	bScheduleWarned = false;
	DrawNext(GetClockSeconds(), !Sync.IsValid());
}

void UWorldWeatherSubsystem::SetLocked(bool bInLocked)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("SetLocked called on a client — ignored."));
		return;
	}
	bLocked = bInLocked;
	Publish();
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: schedule %s."), bLocked ? TEXT("locked") : TEXT("running"));
}

void UWorldWeatherSubsystem::Strike(const FVector& Location)
{
	FlashRemaining = UWorldWeatherSettings::Get()->LightningFlashSeconds;
	OnLightning.Broadcast(Location);
}

void UWorldWeatherSubsystem::SetClimateProvider(const TScriptInterface<IWorldWeatherClimateProvider>& Provider)
{
	ClimateProvider = Provider;
	ExposureCache.Reset();
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: climate provider %s."), Provider ? *Provider.GetObject()->GetName() : TEXT("none (the defaults)"));
}

void UWorldWeatherSubsystem::SetShelterProvider(const TScriptInterface<IWorldWeatherShelterProvider>& Provider)
{
	ShelterProvider = Provider;
	ExposureCache.Reset();
}

// --- Save ---

FWorldWeatherSaveState UWorldWeatherSubsystem::ExportState() const
{
	FWorldWeatherSaveState Out;
	Out.Sync = GetSyncPoint();
	Out.ScheduleIndex = ScheduleIndex;
	Out.bLocked = IsLocked();
	return Out;
}

void UWorldWeatherSubsystem::ImportState(const FWorldWeatherSaveState& InState)
{
	if (!HasAuthority())
	{
		UE_LOG(LogWorldWeather, Warning, TEXT("ImportState called on a client — ignored."));
		return;
	}
	Sync = InState.Sync;
	Sync.Serial = FMath::Max(Sync.Serial, 1); // a loaded point is a point even when it was saved before the first draw
	ScheduleIndex = FMath::Max(InState.ScheduleIndex, 0);
	bLocked = InState.bLocked;
	CurrentPresetIndex = Table ? Table->IndexOfTag(Sync.ToTag) : -1;
	Publish();
	bEdgesPrimed = false; // re-prime so a loaded storm fires no change
	LastStruckMinute = -1;
}

// --- Edges ---

void UWorldWeatherSubsystem::FireEdgeEvents(double Now)
{
	const FGameplayTag Preset = GetCurrentPreset();
	if (!bEdgesPrimed)
	{
		LastPreset = Preset;
		bEdgesPrimed = true;
		return;
	}
	if (Preset != LastPreset)
	{
		const FGameplayTag Old = LastPreset;
		LastPreset = Preset;
		UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: %s -> %s at clock %.0f s."), *Old.ToString(), *Preset.ToString(), Now);
		OnWeatherChanged.Broadcast(Old, Preset);
	}
}

void UWorldWeatherSubsystem::TickLightning(double Now)
{
	const int64 Minute = FMath::FloorToInt64(Now / 60.0);
	if (LastStruckMinute < 0)
	{
		LastStruckMinute = Minute; // primed: the minute already under way strikes nothing
		return;
	}
	if (Minute == LastStruckMinute)
	{
		return;
	}
	float Second = 0.0f;
	if (!UWorldWeatherMath::StrikeInMinute(GetScheduleSeed(), Minute, GetCurrentFront().ThunderRate, Second))
	{
		LastStruckMinute = Minute;
		return;
	}
	if (Now - static_cast<double>(Minute) * 60.0 >= Second)
	{
		// The place is this machine's: around its first local player's view, else the authority's reference point.
		const FVector Centre = GetLocalViewPoint();
		const FVector Up = GetClimateAt(Centre).Up;
		FVector North, East;
		UWorldWeatherMath::LocalFrame(Up, North, East);
		const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
		const float Radius = UWorldWeatherSettings::Get()->LightningRadius * FMath::Sqrt(FMath::FRand());
		Strike(Centre + (North * FMath::Cos(Angle) + East * FMath::Sin(Angle)) * Radius);
		LastStruckMinute = Minute;
	}
}

// --- The sky (P1) ---

void UWorldWeatherSubsystem::FillModifiers(const FWorldWeatherFront& Front, float Flash, const UWorldWeatherSettings& S, FDayNightModifiers& Out)
{
	const float Cover = FMath::Clamp(Front.CloudCover, 0.0f, 1.0f);
	Out.SunIntensityScale = FMath::Max(1.0f - Cover * S.SunDimByCloudCover - FMath::Clamp(Front.Precipitation, 0.0f, 1.0f) * S.SunDimByPrecipitation, S.SunScaleMin);
	Out.SkyLightIntensityScale = FMath::Max(1.0f - Cover * S.SkyLightDimByCloudCover, 0.0f);
	Out.FogDensityScale = 1.0f + FMath::Clamp(Front.FogDensity, 0.0f, 1.0f) * S.FogDensityScaleByFog;
	Out.FogTint = FMath::Lerp(FLinearColor::White, S.FogTintOvercast, Cover);
	Out.CloudCoverage = FMath::Lerp(S.CloudCoverageMin, S.CloudCoverageMax, Cover);
	Out.CloudDensity = FMath::Lerp(S.CloudDensityMin, S.CloudDensityMax, FMath::Clamp(Front.CloudDensity, 0.0f, 1.0f));
	Out.LightningFlash = FMath::Clamp(Flash, 0.0f, 1.0f);
}

float UWorldWeatherSubsystem::MirrorScalar(EWorldWeatherMirrorSource Source, const FWorldWeatherFront& Front, float Wetness, float SnowCover, float TemperatureC, float Gust) const
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	switch (Source)
	{
	case EWorldWeatherMirrorSource::CloudCover: return Front.CloudCover;
	case EWorldWeatherMirrorSource::CloudDensity: return Front.CloudDensity;
	case EWorldWeatherMirrorSource::Precipitation: return Front.Precipitation;
	case EWorldWeatherMirrorSource::FogDensity: return Front.FogDensity;
	case EWorldWeatherMirrorSource::WindSpeed: return Front.WindSpeed;
	case EWorldWeatherMirrorSource::WindIntensity: return FMath::Clamp(Front.WindSpeed / FMath::Max(S->StrongWindAbove, 1.0f), 0.0f, 1.0f);
	case EWorldWeatherMirrorSource::GustFactor: return Gust;
	case EWorldWeatherMirrorSource::Gustiness: return Front.Gustiness;
	case EWorldWeatherMirrorSource::ThunderRate: return Front.ThunderRate;
	case EWorldWeatherMirrorSource::Humidity: return Front.Humidity;
	case EWorldWeatherMirrorSource::Wetness: return Wetness;
	case EWorldWeatherMirrorSource::SnowCover: return SnowCover;
	case EWorldWeatherMirrorSource::TemperatureC: return TemperatureC;
	case EWorldWeatherMirrorSource::LightningFlash: return GetLightningFlash();
	case EWorldWeatherMirrorSource::WindAzimuth: return Front.WindAzimuth;
	case EWorldWeatherMirrorSource::FreezeLevel: return UWorldWeatherMath::FreezeLevel(S->ColdTemperatureC, S->HotTemperatureC, S->SnowBelowC);
	case EWorldWeatherMirrorSource::ColdFactor: return UWorldWeatherMath::ColdFactor(TemperatureC, S->SnowBelowC, S->SleetBandC);
	default: return 0.0f;
	}
}

void UWorldWeatherSubsystem::TickSky(float DeltaTime, double Now)
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	FlashRemaining = FMath::Max(FlashRemaining - DeltaTime, 0.0f);
	const float Flash = GetLightningFlash();
	const FWorldWeatherFront Front = GetCurrentFront();

	// The clock controller's slot (D61): found by class once one exists, looked for again every second until then.
	if (S->bDriveSky)
	{
		if (!SkyController.IsValid() && World->GetTimeSeconds() >= NextControllerSearch)
		{
			NextControllerSearch = World->GetTimeSeconds() + 1.0;
			for (TActorIterator<ADayNightController> It(World); It; ++It)
			{
				SkyController = *It;
				break;
			}
		}
		if (ADayNightController* Controller = SkyController.Get())
		{
			FDayNightModifiers M;
			FillModifiers(Front, Flash, *S, M);
			Controller->SetModifiers(M);
		}
	}

	// MPC_Weather and the mirror table (D64), at this machine's view.
	if (WeatherCollection || MirrorTargets.Num() > 0)
	{
		const FVector View = GetLocalViewPoint();
		const FWorldWeatherClimateSample Climate = GetClimateAt(View);
		const float Gust = UWorldWeatherMath::GustFactor(Now, Front.Gustiness);
		const FVector Wind = UWorldWeatherMath::WindVector(Front.WindSpeed, Front.WindAzimuth, Climate.Up, 0.0f, Now);
		const FVector WindDir = Wind.GetSafeNormal();
		float Wetness = 0.0f, SnowCover = 0.0f;
		if (const FWorldWeatherSyncPoint* Sp = FindSync())
		{
			UWorldWeatherMath::IntegrateAccumulators(*Sp, Now, S->MakeRates(), Wetness, SnowCover);
		}
		const float TemperatureC = GetTemperatureAt(View);
		if (WeatherCollection)
		{
			static const FName Names[] = { TEXT("CloudCover"), TEXT("CloudDensity"), TEXT("Precipitation"), TEXT("FogDensity"), TEXT("WindSpeed"), TEXT("WindIntensity"),
				TEXT("GustFactor"), TEXT("Gustiness"), TEXT("ThunderRate"), TEXT("Humidity"), TEXT("Wetness"), TEXT("SnowCover"), TEXT("TemperatureC"), TEXT("LightningFlash") };
			static const EWorldWeatherMirrorSource Sources[] = { EWorldWeatherMirrorSource::CloudCover, EWorldWeatherMirrorSource::CloudDensity, EWorldWeatherMirrorSource::Precipitation,
				EWorldWeatherMirrorSource::FogDensity, EWorldWeatherMirrorSource::WindSpeed, EWorldWeatherMirrorSource::WindIntensity, EWorldWeatherMirrorSource::GustFactor,
				EWorldWeatherMirrorSource::Gustiness, EWorldWeatherMirrorSource::ThunderRate, EWorldWeatherMirrorSource::Humidity, EWorldWeatherMirrorSource::Wetness,
				EWorldWeatherMirrorSource::SnowCover, EWorldWeatherMirrorSource::TemperatureC, EWorldWeatherMirrorSource::LightningFlash };
			for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
			{
				UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherCollection, Names[i], MirrorScalar(Sources[i], Front, Wetness, SnowCover, TemperatureC, Gust));
			}
			UKismetMaterialLibrary::SetVectorParameterValue(World, WeatherCollection, TEXT("WindDirection"), FLinearColor(WindDir.X, WindDir.Y, WindDir.Z, Front.WindSpeed));
		}
		for (const FWorldWeatherMirrorTarget& T : MirrorTargets)
		{
			if (!T.Collection)
			{
				continue;
			}
			if (T.Source == EWorldWeatherMirrorSource::WindDirection)
			{
				UKismetMaterialLibrary::SetVectorParameterValue(World, T.Collection, T.Parameter, FLinearColor(WindDir.X, WindDir.Y, WindDir.Z, Front.WindSpeed));
			}
			else
			{
				UKismetMaterialLibrary::SetScalarParameterValue(World, T.Collection, T.Parameter, MirrorScalar(T.Source, Front, Wetness, SnowCover, TemperatureC, Gust) * T.Scale + T.Offset);
			}
		}
	}

	UpdateViewers();

	// The cost, for ww.Weather: the last tick's and a one-second average.
	const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
	SkyWriteLastMs = static_cast<float>(Ms);
	SkyWriteWindowSum += Ms;
	GPUFrameWindowSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()); // the last frame's GPU time, for the viewer effects' A/B
	++SkyWriteWindowCount;
	if (World->GetTimeSeconds() - SkyWriteWindowStart >= 1.0)
	{
		SkyWriteAverageMs = static_cast<float>(SkyWriteWindowSum / FMath::Max(SkyWriteWindowCount, 1));
		GPUFrameAverageMs = static_cast<float>(GPUFrameWindowSum / FMath::Max(SkyWriteWindowCount, 1));
		SkyWriteWindowStart = World->GetTimeSeconds();
		SkyWriteWindowSum = 0.0;
		GPUFrameWindowSum = 0.0;
		SkyWriteWindowCount = 0;
	}
}

void UWorldWeatherSubsystem::UpdateViewers()
{
	UWorld* World = GetWorld();
	if (!World || !UWorldWeatherSettings::Get()->bAutoSpawnViewers || IsRunningDedicatedServer())
	{
		return;
	}
	// Drop the viewers whose player is gone.
	for (int32 i = Viewers.Num() - 1; i >= 0; --i)
	{
		AWorldWeatherViewer* V = Viewers[i];
		if (!V || !V->GetPlayer())
		{
			if (V)
			{
				V->Destroy();
			}
			Viewers.RemoveAt(i);
		}
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		const bool bHas = Viewers.ContainsByPredicate([PC](const AWorldWeatherViewer* V) { return V && V->GetPlayer() == PC; });
		if (bHas)
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		AWorldWeatherViewer* Viewer = World->SpawnActor<AWorldWeatherViewer>(AWorldWeatherViewer::StaticClass(), FTransform::Identity, Params);
		if (Viewer)
		{
			Viewer->SetPlayer(PC);
			Viewers.Add(Viewer);
			UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: a viewer for %s."), *PC->GetName());
		}
	}
}

TArray<AWorldWeatherViewer*> UWorldWeatherSubsystem::GetViewers() const
{
	TArray<AWorldWeatherViewer*> Out;
	for (AWorldWeatherViewer* V : Viewers)
	{
		if (V)
		{
			Out.Add(V);
		}
	}
	return Out;
}
