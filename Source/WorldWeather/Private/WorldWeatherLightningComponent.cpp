// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherLightningComponent.h"
#include "WorldWeather.h"
#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UWorldWeatherLightningComponent::UWorldWeatherLightningComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // ticks only while a strike light fades
}

float UWorldWeatherLightningComponent::FlashIntensity(float Age, float Seconds)
{
	const float Left = 1.f - FMath::Clamp(Age / FMath::Max(Seconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
	return Left * Left;
}

FRotator UWorldWeatherLightningComponent::BoltRotation(const FVector& Up)
{
	return FRotationMatrix::MakeFromZ(Up.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector)).Rotator();
}

void UWorldWeatherLightningComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	for (int32 i = Lights.Num() - 1; i >= 0; --i)
	{
		Lights[i].Age += DeltaTime;
		UPointLightComponent* Light = Lights[i].Light.Get();
		const float Factor = FlashIntensity(Lights[i].Age, S->LightningLightSeconds);
		if (Factor <= 0.f)
		{
			if (Light)
			{
				Light->DestroyComponent();
			}
			if (UNiagaraComponent* Bolt = Lights[i].Bolt.Get())
			{
				Bolt->Deactivate(); // the beam's particles die out and the system destroys itself
			}
			Lights.RemoveAtSwap(i);
			continue;
		}
		if (Light)
		{
			Light->SetIntensity(S->LightningLightLumens * Factor);
		}
	}
	if (Lights.Num() == 0)
	{
		SetComponentTickEnabled(false);
	}
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
	for (FStrikeLight& Strike : Lights)
	{
		if (UPointLightComponent* Light = Strike.Light.Get())
		{
			Light->DestroyComponent();
		}
		if (UNiagaraComponent* Bolt = Strike.Bolt.Get())
		{
			Bolt->DestroyComponent();
		}
	}
	Lights.Reset();
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
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	// The bolt stands along the local up at the strike (the conditions there carry it); the light sits up the bolt
	// (LightningLightHeight) so the ground around the strike is lit evenly and nothing stands inside it.
	FVector Up = FVector::UpVector;
	if (const UWorldWeatherSubsystem* Weather = UWorldWeatherSubsystem::Get(this))
	{
		Up = Weather->GetConditionsAt(Location).Up;
	}
	UNiagaraComponent* Bolt = nullptr;
	if (World && BoltSystem)
	{
		Bolt = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, BoltSystem, Location, BoltRotation(Up), FVector::OneVector, /*bAutoDestroy=*/true, /*bAutoActivate=*/true);
	}
	if (World && GetOwner() && (S->LightningLightLumens > 0.f || Bolt))
	{
		UPointLightComponent* Light = nullptr;
		if (S->LightningLightLumens > 0.f)
		{
			Light = NewObject<UPointLightComponent>(GetOwner(), NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(S->LightningLightLumens);
		Light->SetAttenuationRadius(S->LightningLightRadius);
		Light->SetLightColor(S->LightningLightColor);
			Light->SetCastShadows(false);
			Light->bUseInverseSquaredFalloff = true;
			Light->SetWorldLocation(Location + Up * S->LightningLightHeight);
			Light->RegisterComponent();
		}
		Lights.Add({Light, Bolt, 0.f});
		SetComponentTickEnabled(true);
	}
	if (UWorldWeatherAmbienceComponent* A = Ambience.Get())
	{
		A->QueueThunder(static_cast<float>(FVector::Dist(GetComponentLocation(), Location)));
	}
}
