// Copyright Daniel Raquel. All Rights Reserved.

// The weather's console commands (a plugin registers its own diagnostics, CR4 FW-30): ww.Weather, ww.Set, ww.Lock, ww.Wind,
// ww.Strike, ww.Next. The game module's vox.Weather and vox.Wind run ww.Weather and ww.Wind.

#include "WorldWeather.h"
#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeatherLensComponent.h"
#include "WorldWeatherLightningComponent.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherPrecipitationComponent.h"
#include "WorldWeatherPreset.h"
#include "WorldWeatherSubsystem.h"
#include "WorldWeatherTags.h"
#include "WorldWeatherViewer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace WorldWeatherConsole
{
	/** The PIE or Game world: the console's own when it is one, else the first PIE/Game world context. */
	UWorld* FindPlayWorld(UWorld* World)
	{
		if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
				{
					return Ctx.World();
				}
			}
		}
		return nullptr;
	}

	UWorldWeatherSubsystem* FindWeather(UWorld* World)
	{
		UWorldWeatherSubsystem* Weather = World ? World->GetSubsystem<UWorldWeatherSubsystem>() : nullptr;
		if (!Weather)
		{
			UE_LOG(LogWorldWeather, Warning, TEXT("ww.*: no PIE/Game world weather."));
		}
		return Weather;
	}

	const APawn* FindPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	/** "Rain" or "Weather.Preset.Rain" to the tag. */
	FGameplayTag PresetTagFrom(const FString& Text)
	{
		const FString Full = Text.StartsWith(TEXT("Weather.")) ? Text : FString::Printf(TEXT("Weather.Preset.%s"), *Text);
		return FGameplayTag::RequestGameplayTag(FName(*Full), /*ErrorIfNotFound=*/false);
	}

	FString FrontText(const FWorldWeatherFront& F)
	{
		return FString::Printf(TEXT("cover %.2f density %.2f precip %.2f fog %.2f wind %.0f@%.0f gust %.2f thunder %.2f temp %+.1f humidity %.2f"),
			F.CloudCover, F.CloudDensity, F.Precipitation, F.FogDensity, F.WindSpeed, F.WindAzimuth, F.Gustiness, F.ThunderRate, F.TemperatureOffset, F.Humidity);
	}

	void Weather(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindPlayWorld(InWorld);
		UWorldWeatherSubsystem* Weather = FindWeather(World);
		if (!Weather)
		{
			return;
		}
		const FWorldWeatherSyncPoint Sync = Weather->GetSyncPoint();
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: %s (%s -> %s, alpha %.2f, serial %d%s) clock %.0f s; wetness %.2f snow %.2f"),
			*Weather->GetCurrentPreset().ToString(), *Sync.FromTag.ToString(), *Sync.ToTag.ToString(), Weather->GetBlendAlpha(), Sync.Serial,
			Weather->IsLocked() ? TEXT(", locked") : TEXT(""), Weather->GetClockSeconds(), Weather->GetWetness(), Weather->GetSnowCover());
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: front %s"), *FrontText(Weather->GetCurrentFront()));
		float LastMs = 0.0f, AverageMs = 0.0f;
		Weather->GetSkyWriteCost(LastMs, AverageMs);
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: sky controller %s, %d viewer(s), flash %.2f; the sky, collection and mirror writes %.3f ms last tick, %.3f ms average; GPU frame %.2f ms average"),
			Weather->GetSkyController() ? TEXT("driven") : TEXT("none"), Weather->GetViewers().Num(), Weather->GetLightningFlash(), LastMs, AverageMs, Weather->GetGPUFrameAverageMs());
		if (Sync.IsValid())
		{
			UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: blend %.0f s from clock %.0f s, hold %.0f s (next draw at %.0f s)"), Sync.BlendSeconds, Sync.StartSeconds,
				Sync.HoldSeconds, Sync.StartSeconds + Sync.BlendSeconds + Sync.HoldSeconds);
		}
		const APawn* Pawn = FindPawn(World);
		const FVector Here = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		const FWorldWeatherConditions C = Weather->GetConditionsAt(Here, Pawn);
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: at %s (%s): %s rate %.2f, %.1f C, wind %s (%.0f uu/s), wetness %.2f snow %.2f exposure %.2f; tags %s"),
			*Here.ToCompactString(), Pawn ? TEXT("the pawn") : TEXT("the origin"), *UEnum::GetValueAsString(C.PrecipitationType), C.PrecipitationRate, C.TemperatureC,
			*C.WindVector.ToCompactString(), C.WindVector.Size(), C.Wetness, C.SnowCover, C.Exposure, *C.Tags.ToStringSimple());
		const FWorldWeatherClimateSample Climate = Weather->GetClimateAt(Here);
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: climate provider %s, shelter provider %s; climate here T %.2f M %.2f altitude %.0f up %s%s"),
			Weather->GetClimateProvider() ? *Weather->GetClimateProvider().GetObject()->GetClass()->GetName() : TEXT("none (the defaults)"),
			Weather->GetShelterProvider() ? *Weather->GetShelterProvider().GetObject()->GetClass()->GetName() : TEXT("none (the trace)"),
			Climate.Temperature, Climate.Moisture, Climate.Altitude, *Climate.Up.ToCompactString(), Climate.bUnderwater ? TEXT(", under water") : TEXT(""));
		for (const AWorldWeatherViewer* V : Weather->GetViewers())
		{
			const UWorldWeatherPrecipitationComponent* P = V ? V->Precipitation : nullptr;
			if (P)
			{
				UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: viewer %s: precipitation %s rate %.2f (at the view: %s rate %.2f exposure %.2f), occlusion %s, %d captures"),
					*V->GetName(), *UEnum::GetValueAsString(P->GetActiveType()), P->GetActiveRate(), *UEnum::GetValueAsString(V->GetViewConditions().PrecipitationType),
					V->GetViewConditions().PrecipitationRate, V->GetViewConditions().Exposure, P->IsOcclusionCapturing() ? TEXT("capturing") : TEXT("off"), P->GetCaptureCount());
			}
			if (V && V->Ambience && V->Lens && V->Lightning)
			{
				const FVector Volumes = V->Ambience->GetVolumes();
				const FVector Inputs = V->Lens->GetInputs();
				UE_LOG(LogWorldWeather, Display, TEXT("ww.Weather: viewer %s: ambience rain %.2f / %.2f wind %.2f low-pass %.0f Hz, thunder %d pending %d played; lens weight %.2f (drops %.2f frost %.2f wet %.2f); %d strike(s)"),
					*V->GetName(), Volumes.X, Volumes.Y, Volumes.Z, V->Ambience->GetLowPassHz(), V->Ambience->GetPendingThunder(), V->Ambience->GetThunderPlayed(),
					V->Lens->GetWeight(), Inputs.X, Inputs.Y, Inputs.Z, V->Lightning->GetStrikeCount());
			}
		}
	}

	void Set(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorldWeatherSubsystem* Weather = FindWeather(FindPlayWorld(InWorld));
		if (!Weather || Args.Num() < 1)
		{
			FString Names;
			if (Weather && Weather->GetPresetTable())
			{
				for (const FWorldWeatherPreset& P : Weather->GetPresetTable()->Presets)
				{
					Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + P.Tag.ToString();
				}
			}
			UE_LOG(LogWorldWeather, Display, TEXT("ww.Set: usage ww.Set <Preset> [BlendSeconds] (authority). Presets: %s"), *Names);
			return;
		}
		const FGameplayTag Tag = PresetTagFrom(Args[0]);
		if (!Tag.IsValid())
		{
			UE_LOG(LogWorldWeather, Warning, TEXT("ww.Set: %s is not a weather preset tag."), *Args[0]);
			return;
		}
		const float Blend = Args.Num() >= 2 ? FCString::Atof(*Args[1]) : -1.0f;
		if (Weather->SetWeather(Tag, Blend))
		{
			UE_LOG(LogWorldWeather, Display, TEXT("ww.Set: %s over %.0f s."), *Tag.ToString(), Weather->GetSyncPoint().BlendSeconds);
		}
	}

	void Lock(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorldWeatherSubsystem* Weather = FindWeather(FindPlayWorld(InWorld));
		if (!Weather || Args.Num() < 1)
		{
			UE_LOG(LogWorldWeather, Display, TEXT("ww.Lock: usage ww.Lock 0|1 (authority; 1 stops the schedule)"));
			return;
		}
		Weather->SetLocked(FCString::Atoi(*Args[0]) != 0);
	}

	void Wind(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorldWeatherSubsystem* Weather = FindWeather(FindPlayWorld(InWorld));
		if (!Weather || Args.Num() < 2)
		{
			UE_LOG(LogWorldWeather, Display, TEXT("ww.Wind: usage ww.Wind <AzimuthDegrees> <SpeedUUPerSecond> (authority)"));
			return;
		}
		Weather->SetWind(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
	}

	void Strike(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindPlayWorld(InWorld);
		UWorldWeatherSubsystem* Weather = FindWeather(World);
		if (!Weather)
		{
			return;
		}
		const APawn* Pawn = FindPawn(World);
		const FVector Here = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		const float Distance = Args.Num() >= 1 ? FCString::Atof(*Args[0]) : 3000.0f;
		FVector North, East;
		UWorldWeatherMath::LocalFrame(Weather->GetClimateAt(Here).Up, North, East);
		const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
		const FVector Place = Here + (North * FMath::Cos(Angle) + East * FMath::Sin(Angle)) * Distance;
		Weather->Strike(Place);
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Strike: lightning at %s (%.0f uu from %s)."), *Place.ToCompactString(), Distance, Pawn ? TEXT("the pawn") : TEXT("the origin"));
	}

	void Next(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorldWeatherSubsystem* Weather = FindWeather(FindPlayWorld(InWorld));
		if (!Weather)
		{
			return;
		}
		Weather->SkipToNext();
		const FWorldWeatherSyncPoint Sync = Weather->GetSyncPoint();
		UE_LOG(LogWorldWeather, Display, TEXT("ww.Next: %s -> %s over %.0f s, hold %.0f s."), *Sync.FromTag.ToString(), *Sync.ToTag.ToString(), Sync.BlendSeconds, Sync.HoldSeconds);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherWeatherCmd(
	TEXT("ww.Weather"),
	TEXT("Print the weather: the present preset and blend, the front, the accumulators, the next draw; the conditions where the pawn stands"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Weather(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherSetCmd(
	TEXT("ww.Set"),
	TEXT("Blend to a preset: ww.Set <Preset|Weather.Preset.X> [BlendSeconds] (authority; no argument lists the presets)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Set(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherLockCmd(
	TEXT("ww.Lock"),
	TEXT("Stop (1) or resume (0) the weather schedule: ww.Lock 0|1 (authority)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Lock(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherWindCmd(
	TEXT("ww.Wind"),
	TEXT("Turn the wind: ww.Wind <AzimuthDegrees> <SpeedUUPerSecond> (authority; degrees from north toward east, a 30-game-second blend)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Wind(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherStrikeCmd(
	TEXT("ww.Strike"),
	TEXT("A lightning strike near the pawn: ww.Strike [DistanceUU=3000] (this machine only)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Strike(Args, World); }));

static FAutoConsoleCommandWithWorldAndArgs GWorldWeatherNextCmd(
	TEXT("ww.Next"),
	TEXT("Draw the next scheduled front now (authority)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { WorldWeatherConsole::Next(Args, World); }));

#endif // !UE_BUILD_SHIPPING
