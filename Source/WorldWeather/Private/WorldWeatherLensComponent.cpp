// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherLensComponent.h"
#include "WorldWeather.h"
#include "WorldWeatherSettings.h"

#include "Components/PostProcessComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static TAutoConsoleVariable<int32> CVarWorldWeatherViewerLens(
	TEXT("ww.Viewer.Lens"), 1,
	TEXT("The weather on each local player's lens (drops, frost, a wet darkening; 1 on, 0 off: A/B)"),
	ECVF_Default);

const FName UWorldWeatherLensComponent::DropsParameter(TEXT("Drops"));
const FName UWorldWeatherLensComponent::FrostParameter(TEXT("Frost"));
const FName UWorldWeatherLensComponent::WetParameter(TEXT("Wet"));

UWorldWeatherLensComponent::UWorldWeatherLensComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the viewer drives it
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(this);
	PostProcess->bUnbound = true;
	PostProcess->Priority = 10.0f; // over the clock's exposure and the dungeon's interior volumes
	PostProcess->BlendWeight = 0.0f;
	PostProcess->bEnabled = true;
}

void UWorldWeatherLensComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	const FSoftObjectPath Path = S->LensMaterial.ToSoftObjectPath();
	UMaterialInterface* Material = Cast<UMaterialInterface>(UWorldWeatherSettings::LoadIfPresent(Path));
	if (Material && PostProcess)
	{
		Instance = UMaterialInstanceDynamic::Create(Material, this);
		PostProcess->Settings.WeightedBlendables.Array.Reset();
		PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Instance));
	}
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: lens on %s: %s."), *GetOwner()->GetName(), Material ? *Material->GetName() : TEXT("no material (the lens is off)"));
}

void UWorldWeatherLensComponent::UpdateWeather(const FWorldWeatherConditions& C, float ColdFactor, float LookUp, float DeltaTime)
{
	if (!PostProcess || !Instance)
	{
		return;
	}
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	const bool bOn = CVarWorldWeatherViewerLens.GetValueOnGameThread() != 0;
	const float Exposure = FMath::Clamp(C.Exposure, 0.0f, 1.0f);
	// Drops land on the lens when it faces the falling rain: full looking straight up, none below the horizon; snow puts fewer.
	const float Facing = FMath::Clamp(LookUp * 1.5f + 0.25f, 0.0f, 1.0f);
	const float Falls = C.PrecipitationType == EWorldWeatherPrecipitation::None ? 0.0f : (C.PrecipitationType == EWorldWeatherPrecipitation::Snow ? 0.4f : 1.0f);
	const float TargetDrops = bOn ? FMath::Clamp(C.PrecipitationRate, 0.0f, 1.0f) * Exposure * Facing * Falls : 0.0f;
	const float TargetFrost = bOn ? FMath::Clamp(ColdFactor, 0.0f, 1.0f) * (0.4f + 0.6f * Exposure) : 0.0f;
	const float TargetWet = bOn ? FMath::Clamp(C.Wetness, 0.0f, 1.0f) * Exposure : 0.0f;
	// The inputs move smoothly too (a turn of the head should not snap the drops on).
	LastDrops = FMath::FInterpTo(LastDrops, TargetDrops, DeltaTime, S->LensBlendSpeed * 2.0f);
	LastFrost = FMath::FInterpTo(LastFrost, TargetFrost, DeltaTime, S->LensBlendSpeed);
	LastWet = FMath::FInterpTo(LastWet, TargetWet, DeltaTime, S->LensBlendSpeed);
	Instance->SetScalarParameterValue(DropsParameter, LastDrops);
	Instance->SetScalarParameterValue(FrostParameter, LastFrost);
	Instance->SetScalarParameterValue(WetParameter, LastWet);
	const float TargetWeight = FMath::Clamp(FMath::Max3(LastDrops * 3.0f, LastFrost, LastWet * 0.6f), 0.0f, 1.0f);
	Weight = FMath::FInterpTo(Weight, TargetWeight, DeltaTime, S->LensBlendSpeed);
	if (Weight < 0.002f)
	{
		Weight = 0.0f;
	}
	PostProcess->BlendWeight = Weight;
}
