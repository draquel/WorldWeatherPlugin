// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherViewer.h"
#include "WorldWeather.h"
#include "WorldWeatherPrecipitationComponent.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LocalFogVolumeComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

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
	// Starts clear: the radial density is the weather's fog, written every tick; no height fog in the volume (the sphere does it).
	LocalFog->RadialFogExtinction = 0.0f;
	LocalFog->HeightFogExtinction = 0.0f;

	Precipitation = CreateDefaultSubobject<UWorldWeatherPrecipitationComponent>(TEXT("Precipitation"));
	Precipitation->SetupAttachment(Root);
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
	// The fog about the camera: the front's fog density as the volume's radial extinction, the sphere the settings' radius.
	const float Fog = CVarWorldWeatherViewerFog.GetValueOnGameThread() != 0 ? Weather->GetCurrentFront().FogDensity * Settings->LocalFogExtinctionMax : 0.0f;
	if (Fog != LastFogExtinction)
	{
		LocalFog->SetRadialFogExtinction(Fog);
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
}
