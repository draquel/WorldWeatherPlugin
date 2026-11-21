// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherPrecipitationComponent.h"
#include "WorldWeather.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

static TAutoConsoleVariable<int32> CVarWorldWeatherViewerPrecipitation(
	TEXT("ww.Viewer.Precipitation"), 1,
	TEXT("The precipitation particles about each local player's camera (1 on, 0 off: A/B)"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarWorldWeatherViewerOcclusion(
	TEXT("ww.Viewer.Occlusion"), 1,
	TEXT("The precipitation's top-down depth capture (1 on, 0 off: the particles then fade with the viewer's traced exposure)"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarWorldWeatherOcclusionFrames(
	TEXT("ww.Occlusion.Frames"), -1,
	TEXT("Frames between the precipitation's depth captures (-1 = the settings' OcclusionFrames)"),
	ECVF_Default);

const FName UWorldWeatherPrecipitationComponent::RateParameter(TEXT("User.Rate"));
const FName UWorldWeatherPrecipitationComponent::WindParameter(TEXT("User.Wind"));
const FName UWorldWeatherPrecipitationComponent::UpParameter(TEXT("User.Up"));

UWorldWeatherPrecipitationComponent::UWorldWeatherPrecipitationComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the viewer drives it

	Particles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Particles"));
	Particles->SetupAttachment(this);
	Particles->bAutoActivate = false;
	Particles->SetAutoDestroy(false);

	Occlusion = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Occlusion"));
	Occlusion->SetupAttachment(this);
	Occlusion->SetUsingAbsoluteRotation(true); // the capture keeps the local frame while the particles lean with the wind
	Occlusion->ProjectionType = ECameraProjectionMode::Orthographic;
	Occlusion->bAutoCalculateOrthoPlanes = false; // the planes stay 0 .. the world's far: the ground any distance below is in range
	Occlusion->bUpdateOrthoPlanes = false;
	Occlusion->CaptureSource = SCS_SceneDepth;
	Occlusion->bCaptureEveryFrame = false;
	Occlusion->bCaptureOnMovement = false;
	Occlusion->bAlwaysPersistRenderingState = false;
	Occlusion->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	// Depth only: nothing that lights, fogs or post-processes, and no particles or translucency (rain must not occlude rain).
	FEngineShowFlags& Flags = Occlusion->ShowFlags;
	Flags.SetLighting(false);
	Flags.SetDynamicShadows(false);
	Flags.SetSkyLighting(false);
	Flags.SetAtmosphere(false);
	Flags.SetFog(false);
	Flags.SetVolumetricFog(false);
	Flags.SetPostProcessing(false);
	Flags.SetAntiAliasing(false);
	Flags.SetBloom(false);
	Flags.SetMotionBlur(false);
	Flags.SetAmbientOcclusion(false);
	Flags.SetScreenSpaceReflections(false);
	Flags.SetLumenGlobalIllumination(false);
	Flags.SetLumenReflections(false);
	Flags.SetTranslucency(false);
	Flags.SetParticles(false);
	Flags.SetDecals(false);
	Flags.SetCloud(false);
}

void UWorldWeatherPrecipitationComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	RainSystem = Cast<UNiagaraSystem>(UWorldWeatherSettings::LoadIfPresent(S->RainSystem.ToSoftObjectPath()));
	SnowSystem = Cast<UNiagaraSystem>(UWorldWeatherSettings::LoadIfPresent(S->SnowSystem.ToSoftObjectPath()));
	OcclusionTarget = Cast<UTextureRenderTarget2D>(UWorldWeatherSettings::LoadIfPresent(S->OcclusionTarget.ToSoftObjectPath()));
	Collection = Cast<UMaterialParameterCollection>(UWorldWeatherSettings::LoadIfPresent(S->WeatherCollection.ToSoftObjectPath()));
	if (Occlusion)
	{
		Occlusion->TextureTarget = OcclusionTarget;
		Occlusion->OrthoWidth = 2.0f * S->OcclusionExtent;
		if (AActor* Owner = GetOwner())
		{
			Occlusion->HiddenActors.AddUnique(Owner);
		}
	}
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather: precipitation on %s: rain %s, snow %s, occlusion target %s (%s)."), *GetOwner()->GetName(),
		RainSystem ? *RainSystem->GetName() : TEXT("none"), SnowSystem ? *SnowSystem->GetName() : TEXT("none"),
		OcclusionTarget ? *OcclusionTarget->GetName() : TEXT("none"), Collection ? *Collection->GetName() : TEXT("no collection"));
}

void UWorldWeatherPrecipitationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	WriteOcclusionOff();
	Super::EndPlay(EndPlayReason);
}

UNiagaraSystem* UWorldWeatherPrecipitationComponent::SystemFor(EWorldWeatherPrecipitation Type) const
{
	switch (Type)
	{
	case EWorldWeatherPrecipitation::Rain:
	case EWorldWeatherPrecipitation::Sleet:
		return RainSystem;
	case EWorldWeatherPrecipitation::Snow:
		return SnowSystem;
	default:
		return nullptr;
	}
}

void UWorldWeatherPrecipitationComponent::SetUserFloat(const FName& Name, float Value) const
{
	const UNiagaraSystem* Asset = Particles ? Particles->GetAsset() : nullptr;
	if (Asset && Asset->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), Name)) != INDEX_NONE)
	{
		Particles->SetVariableFloat(Name, Value);
	}
}

void UWorldWeatherPrecipitationComponent::SetUserVector(const FName& Name, const FVector& Value) const
{
	const UNiagaraSystem* Asset = Particles ? Particles->GetAsset() : nullptr;
	if (Asset && Asset->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), Name)) != INDEX_NONE)
	{
		Particles->SetVariableVec3(Name, Value);
	}
}

void UWorldWeatherPrecipitationComponent::UpdateWeather(const FWorldWeatherConditions& C, const FVector& View, float DeltaTime)
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	UWorld* World = GetWorld();
	if (!World || !Particles)
	{
		return;
	}
	const bool bParticlesOn = CVarWorldWeatherViewerPrecipitation.GetValueOnGameThread() != 0;
	const EWorldWeatherPrecipitation Type = C.PrecipitationType;
	UNiagaraSystem* System = bParticlesOn && C.PrecipitationRate > S->PrecipitationActivateAbove ? SystemFor(Type) : nullptr;

	// The system: swapped on a type change, deactivated (its particles dying out) when nothing falls.
	if (!System)
	{
		if (ActiveType != EWorldWeatherPrecipitation::None)
		{
			Particles->Deactivate();
			ActiveType = EWorldWeatherPrecipitation::None;
		}
	}
	else
	{
		if (Particles->GetAsset() != System)
		{
			Particles->SetAsset(System);
		}
		if (ActiveType != Type || !Particles->IsActive())
		{
			Particles->Activate(/*bReset=*/ActiveType == EWorldWeatherPrecipitation::None);
			ActiveType = Type;
		}
	}
	const bool bActive = ActiveType != EWorldWeatherPrecipitation::None;

	// The capture's frame and a capture when due; whether the material can occlude this tick.
	UpdateOcclusion(View, C.Up, bActive);

	// The rate the material reads: the rate at the view, by the traced exposure when the capture is not there to do it.
	ActiveRate = bActive ? C.PrecipitationRate * (bCapturing ? 1.0f : C.Exposure) : 0.0f;
	if (Collection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("LocalPrecipitation"), ActiveRate);
	}
	if (!bActive)
	{
		return;
	}

	// The fall leans with the wind: the system's local -Z is the fall direction (the box spawns and the velocities are local).
	const float FallSpeed = Type == EWorldWeatherPrecipitation::Snow ? S->SnowFallSpeed : S->RainFallSpeed;
	const FVector Fall = (-C.Up * FallSpeed + C.WindVector).GetSafeNormal(KINDA_SMALL_NUMBER, -C.Up);
	FVector North, East;
	UWorldWeatherMath::LocalFrame(C.Up, North, East);
	Particles->SetWorldRotation(FRotationMatrix::MakeFromZY(-Fall, North).Rotator());
	SetUserFloat(RateParameter, C.PrecipitationRate);
	SetUserVector(WindParameter, C.WindVector);
	SetUserVector(UpParameter, C.Up);
}

void UWorldWeatherPrecipitationComponent::UpdateOcclusion(const FVector& View, const FVector& Up, bool bActive)
{
	const UWorldWeatherSettings* S = UWorldWeatherSettings::Get();
	UWorld* World = GetWorld();
	const bool bWanted = bActive && S->bOcclusionCapture && CVarWorldWeatherViewerOcclusion.GetValueOnGameThread() != 0 && Occlusion && OcclusionTarget && World;
	if (!bWanted)
	{
		if (bCapturing)
		{
			WriteOcclusionOff();
		}
		bCapturing = false;
		return;
	}
	FVector Origin, Forward, U, V, CameraUp;
	UWorldWeatherMath::OcclusionFrame(View, Up, S->OcclusionHeight, S->OcclusionExtent, Origin, Forward, U, V, CameraUp);
	Occlusion->SetWorldLocationAndRotation(Origin, FRotationMatrix::MakeFromXZ(Forward, CameraUp).Rotator());
	Occlusion->OrthoWidth = 2.0f * S->OcclusionExtent;

	const int32 Frames = CVarWorldWeatherOcclusionFrames.GetValueOnGameThread() > 0 ? CVarWorldWeatherOcclusionFrames.GetValueOnGameThread() : FMath::Max(S->OcclusionFrames, 1);
	if (!bCapturing || GFrameCounter - LastCaptureFrame >= static_cast<uint64>(Frames))
	{
		Occlusion->CaptureSceneDeferred();
		LastCaptureFrame = GFrameCounter;
		++CaptureCount;
	}
	bCapturing = true;
	if (Collection)
	{
		// The frame the material sums with (UWorldWeatherMath::OcclusionCoords): the origin with the width, U and V per unit,
		// the forward with the bias; V's w = 1 says the capture is live.
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("OcclusionOrigin"), FLinearColor(Origin.X, Origin.Y, Origin.Z, 2.0f * S->OcclusionExtent));
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("OcclusionU"), FLinearColor(U.X, U.Y, U.Z, 0.0f));
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("OcclusionV"), FLinearColor(V.X, V.Y, V.Z, 1.0f));
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("OcclusionForward"), FLinearColor(Forward.X, Forward.Y, Forward.Z, S->OcclusionBias));
	}
}

void UWorldWeatherPrecipitationComponent::WriteOcclusionOff() const
{
	UWorld* World = GetWorld();
	if (World && Collection)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("OcclusionV"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}
}
