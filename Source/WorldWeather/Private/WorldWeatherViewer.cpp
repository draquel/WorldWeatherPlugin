// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherViewer.h"
#include "WorldWeather.h"
#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeatherLensComponent.h"
#include "WorldWeatherLightningComponent.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherPrecipitationComponent.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LocalFogVolumeComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

// The local fog volume's radial extinction while the weather has any fog: dense, so the radial coverage is 1 but for the
// outer few percent of the radius, where it fades the sphere's edge (the height term is the fog itself; see Tick).
static constexpr float LocalFogEdgeExtinction = 100.0f;

static TAutoConsoleVariable<int32> CVarWorldWeatherViewerFog(
	TEXT("ww.Viewer.Fog"), 1,
	TEXT("The weather's local fog volume about each local player's camera (1 on, 0 off: A/B)"),
	ECVF_Default);

AWorldWeatherViewer::AWorldWeatherViewer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the camera has moved this frame
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LocalFog = CreateDefaultSubobject<ULocalFogVolumeComponent>(TEXT("LocalFog"));
	LocalFog->SetupAttachment(Root);
	// Starts clear; the tick writes the weather's density into the height term and the edge into the radial one (see Tick).
	// The lowest falloff keeps the height density within a percent across the sphere, so it is uniform about the camera.
	LocalFog->RadialFogExtinction = 0.0f;
	LocalFog->HeightFogExtinction = 0.0f;
	LocalFog->HeightFogFalloff = 1.0f;
	LocalFog->HeightFogOffset = 0.0f;

	Precipitation = CreateDefaultSubobject<UWorldWeatherPrecipitationComponent>(TEXT("Precipitation"));
	Precipitation->SetupAttachment(Root);

	// P4: what the player hears, what is on the lens, the bolt and thunder of a strike.
	Ambience = CreateDefaultSubobject<UWorldWeatherAmbienceComponent>(TEXT("Ambience"));
	Ambience->SetupAttachment(Root);
	Lens = CreateDefaultSubobject<UWorldWeatherLensComponent>(TEXT("Lens"));
	Lens->SetupAttachment(Root);
	Lightning = CreateDefaultSubobject<UWorldWeatherLightningComponent>(TEXT("Lightning"));
	Lightning->SetupAttachment(Root);
	Lightning->SetAmbience(Ambience);
}

void AWorldWeatherViewer::SetPlayer(APlayerController* InPlayer)
{
	Player = InPlayer;
}

bool AWorldWeatherViewer::GetViewLocation(FVector& OutLocation) const
{
	const APlayerController* PC = Player.Get();
	if (!PC)
	{
		return false;
	}
	if (PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	if (const APawn* Pawn = PC->GetPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}
	return false;
}

void AWorldWeatherViewer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FVector View;
	if (!GetViewLocation(View))
	{
		return;
	}
	SetActorLocation(View);
	const UWorldWeatherSubsystem* Weather = UWorldWeatherSubsystem::Get(this);
	const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
	if (!Weather || !LocalFog)
	{
		return;
	}
	// The fog about the camera, the sphere the settings' radius. The engine combines the volume's two densities as
	// coverages, T = 1 - (1 - TR) * (1 - TH) (LocalFogVolumeCommon.ush), so a radial-only volume draws nothing (G134):
	// the height term carries the weather's density, per metre and uniform across the sphere (falloff 1), and the radial
	// term, dense, is only the sphere's soft edge. Both at 0 when clear: the renderer then skips the instance.
	const float Fog = CVarWorldWeatherViewerFog.GetValueOnGameThread() != 0 ? Weather->GetCurrentFront().FogDensity * Settings->LocalFogExtinctionMax : 0.0f;
	if (Fog != LastFogExtinction)
	{
		LocalFog->SetHeightFogExtinction(Fog);
		LocalFog->SetRadialFogExtinction(Fog > 0.0f ? LocalFogEdgeExtinction : 0.0f);
		LastFogExtinction = Fog;
	}
	const float Scale = FMath::Max(Settings->LocalFogRadius, 500.0f) / ULocalFogVolumeComponent::GetBaseVolumeSize();
	if (!LocalFog->GetRelativeScale3D().Equals(FVector(Scale)))
	{
		LocalFog->SetRelativeScale3D(FVector(Scale));
		LocalFog->SetFogAlbedo(Settings->LocalFogAlbedo);
		LocalFog->SetFogPhaseG(Settings->LocalFogPhaseG);
	}

	// The precipitation (P2): what the weather is at the camera (this actor is the requester: its exposure is cached, it is
	// ignored by the shelter trace).
	ViewConditions = Weather->GetConditionsAt(View, this);
	if (Precipitation)
	{
		Precipitation->UpdateWeather(ViewConditions, View, DeltaSeconds);
	}

	// P4: the ambience by the wind's intensity (the gust is in the vector), the lens by the cold and the camera's look.
	const float WindIntensity = FMath::Clamp(static_cast<float>(ViewConditions.WindVector.Size()) / FMath::Max(Settings->StrongWindAbove, 1.0f), 0.0f, 1.0f);
	if (Ambience)
	{
		Ambience->UpdateWeather(ViewConditions, WindIntensity, DeltaSeconds);
	}
	if (Lens)
	{
		const APlayerController* PC = Player.Get();
		const FVector Forward = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
		const float LookUp = static_cast<float>(FVector::DotProduct(Forward, ViewConditions.Up));
		const float Cold = UWorldWeatherMath::ColdFactor(ViewConditions.TemperatureC, Settings->SnowBelowC, Settings->SleetBandC);
		Lens->UpdateWeather(ViewConditions, Cold, LookUp, DeltaSeconds);
	}
}
