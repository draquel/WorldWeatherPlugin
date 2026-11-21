// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeather.h"
#include "WorldWeatherSettings.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

static TAutoConsoleVariable<int32> CVarWorldWeatherViewerAudio(
	TEXT("ww.Viewer.Audio"), 1,
	TEXT("The weather's ambience (the rain and wind loops, the thunder) for each local player (1 on, 0 off: A/B)"),
	ECVF_Default);

const FName UWorldWeatherAmbienceComponent::RateParameter(TEXT("Rate"));
const FName UWorldWeatherAmbienceComponent::WindParameter(TEXT("Wind"));
const FName UWorldWeatherAmbienceComponent::ExposureParameter(TEXT("Exposure"));

namespace
{
	UAudioComponent* MakeLoop(UObject* Outer, const TCHAR* Name, USceneComponent* Parent)
	{
		UAudioComponent* Loop = Outer->CreateDefaultSubobject<UAudioComponent>(Name);
		Loop->SetupAttachment(Parent);
		Loop->bAutoActivate = false;
		Loop->bAllowSpatialization = false; // the weather is everywhere about the listener
		Loop->bIsUISound = false;
		Loop->bAutoDestroy = false;
		Loop->bStopWhenOwnerDestroyed = true;
		return Loop;
	}
}

UWorldWeatherAmbienceComponent::UWorldWeatherAmbienceComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the viewer drives it
	RainLight = MakeLoop(this, TEXT("RainLight"), this);
	RainHeavy = MakeLoop(this, TEXT("RainHeavy"), this);
	Wind = MakeLoop(this, TEXT("Wind"), this);
}

void UWorldWeatherAmbienceComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	if (RainLight)
	{
		RainLight->SetSound(Cast<USoundBase>(UWorldWeatherSettings::LoadIfPresent(S->RainLightSound.ToSoftObjectPath())));
	}
	if (RainHeavy)
	{
		RainHeavy->SetSound(Cast<USoundBase>(UWorldWeatherSettings::LoadIfPresent(S->RainHeavySound.ToSoftObjectPath())));
	}
	if (Wind)
	{
		Wind->SetSound(Cast<USoundBase>(UWorldWeatherSettings::LoadIfPresent(S->WindSound.ToSoftObjectPath())));
	}
	ThunderSound = Cast<USoundBase>(UWorldWeatherSettings::LoadIfPresent(S->ThunderSound.ToSoftObjectPath()));
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: ambience on %s: rain %s / %s, wind %s, thunder %s."), *GetOwner()->GetName(),
		RainLight && RainLight->Sound ? *RainLight->Sound->GetName() : TEXT("none"), RainHeavy && RainHeavy->Sound ? *RainHeavy->Sound->GetName() : TEXT("none"),
		Wind && Wind->Sound ? *Wind->Sound->GetName() : TEXT("none"), ThunderSound ? *ThunderSound->GetName() : TEXT("none"));
}

void UWorldWeatherAmbienceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (UAudioComponent* Loop : { RainLight.Get(), RainHeavy.Get(), Wind.Get() })
	{
		if (Loop && Loop->IsPlaying())
		{
			Loop->Stop();
		}
	}
	PendingThunder.Reset();
	Super::EndPlay(EndPlayReason);
}

void UWorldWeatherAmbienceComponent::DriveLoop(UAudioComponent* Loop, float Volume, float LowPassHz, float& LastVolume) const
{
	if (!Loop || !Loop->Sound)
	{
		LastVolume = 0.0f;
		return;
	}
	const float V = FMath::Clamp(Volume, 0.0f, 2.0f);
	if (V > 0.005f)
	{
		if (!Loop->IsPlaying())
		{
			Loop->SetVolumeMultiplier(V);
			Loop->FadeIn(0.5f, V);
		}
		else
		{
			Loop->SetVolumeMultiplier(V);
		}
		Loop->SetLowPassFilterEnabled(LowPassHz < 19999.0f);
		Loop->SetLowPassFilterFrequency(LowPassHz);
	}
	else if (Loop->IsPlaying())
	{
		Loop->FadeOut(1.0f, 0.0f);
	}
	LastVolume = V;
}

void UWorldWeatherAmbienceComponent::UpdateWeather(const FWorldWeatherConditions& C, float WindIntensity, float DeltaTime)
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	UWorld* World = GetWorld();
	const bool bOn = CVarWorldWeatherViewerAudio.GetValueOnGameThread() != 0;
	const float Exposure = FMath::Clamp(C.Exposure, 0.0f, 1.0f);
	const float Shelter = 1.0f - Exposure;
	const float Master = bOn ? S->AmbienceVolume * (1.0f - Shelter * S->InteriorVolumeDrop) : 0.0f;
	const float LowPass = FMath::Lerp(20000.0f, S->InteriorLowPassHz, Shelter);

	// The rain: the rate at the view, as light rain up to half and heavy past it (snow is quieter: a third).
	const bool bFalls = C.PrecipitationType != EWorldWeatherPrecipitation::None;
	const float Rate = bFalls ? FMath::Clamp(C.PrecipitationRate, 0.0f, 1.0f) * (C.PrecipitationType == EWorldWeatherPrecipitation::Snow ? 0.33f : 1.0f) : 0.0f;
	const float Heavy = FMath::Clamp((Rate - 0.5f) * 2.0f, 0.0f, 1.0f);
	const float Light = FMath::Clamp(Rate * 2.0f, 0.0f, 1.0f) * (RainHeavy && RainHeavy->Sound ? 1.0f - Heavy : 1.0f);
	DriveLoop(RainLight, Light * Master, LowPass, LastRainLight);
	DriveLoop(RainHeavy, Heavy * Master, LowPass, LastRainHeavy);
	DriveLoop(Wind, FMath::Clamp(WindIntensity, 0.0f, 1.0f) * Master, LowPass, LastWind);
	LastLowPassHz = LowPass;

	// A MetaSound source reads the conditions as inputs (a plain wave ignores them).
	for (UAudioComponent* Loop : { RainLight.Get(), RainHeavy.Get(), Wind.Get() })
	{
		if (Loop && Loop->IsPlaying())
		{
			Loop->SetFloatParameter(RateParameter, Rate);
			Loop->SetFloatParameter(WindParameter, WindIntensity);
			Loop->SetFloatParameter(ExposureParameter, Exposure);
		}
	}

	// The thunder that is due.
	if (World && PendingThunder.Num() > 0)
	{
		const double Now = World->GetTimeSeconds();
		for (int32 i = PendingThunder.Num() - 1; i >= 0; --i)
		{
			if (Now >= PendingThunder[i].PlayAt)
			{
				if (bOn && ThunderSound)
				{
					UGameplayStatics::PlaySound2D(World, ThunderSound, PendingThunder[i].Volume * S->AmbienceVolume * (1.0f - Shelter * S->InteriorVolumeDrop));
				}
				++ThunderPlayed;
				PendingThunder.RemoveAt(i);
			}
		}
	}
}

void UWorldWeatherAmbienceComponent::QueueThunder(float Distance)
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	const UWorld* World = GetWorld();
	if (!World || Distance > S->ThunderMaxDistance)
	{
		return;
	}
	FPendingThunder& T = PendingThunder.AddDefaulted_GetRef();
	T.PlayAt = World->GetTimeSeconds() + static_cast<double>(FMath::Max(Distance, 0.0f) / FMath::Max(S->SpeedOfSound, 100.0f));
	T.Volume = FMath::Clamp(1.0f - Distance / S->ThunderMaxDistance, 0.05f, 1.0f);
}
