// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherLightningComponent.h"
#include "WorldWeather.h"
#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"

#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UWorldWeatherLightningComponent::UWorldWeatherLightningComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldWeatherLightningComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	const FSoftObjectPath Path = S->LightningSystem.ToSoftObjectPath();
	BoltSystem = Cast<UNiagaraSystem>(UWorldWeatherSettings::LoadIfPresent(Path));
	if (UWorldWeatherSubsystem* Weather = UWorldWeatherSubsystem::Get(this))
	{
		Weather->OnLightning.AddDynamic(this, &UWorldWeatherLightningComponent::OnStrike);
	}
}

void UWorldWeatherLightningComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldWeatherSubsystem* Weather = UWorldWeatherSubsystem::Get(this))
	{
		Weather->OnLightning.RemoveDynamic(this, &UWorldWeatherLightningComponent::OnStrike);
	}
	Super::EndPlay(EndPlayReason);
}

void UWorldWeatherLightningComponent::OnStrike(FVector Location)
{
	++StrikeCount;
	LastStrike = Location;
	UWorld* World = GetWorld();
	if (World && BoltSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, BoltSystem, Location, FRotator::ZeroRotator, FVector::OneVector, /*bAutoDestroy=*/true, /*bAutoActivate=*/true);
	}
	if (UWorldWeatherAmbienceComponent* A = Ambience.Get())
	{
		A->QueueThunder(static_cast<float>(FVector::Dist(GetComponentLocation(), Location)));
	}
}
