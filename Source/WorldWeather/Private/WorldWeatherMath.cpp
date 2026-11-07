// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherMath.h"
#include "WorldWeatherTags.h"
#include "Math/RandomStream.h"

float UWorldWeatherMath::LerpAzimuth(float FromDegrees, float ToDegrees, float Alpha)
{
	const float Delta = FMath::FindDeltaAngleDegrees(FromDegrees, ToDegrees);
	const float Result = FMath::Fmod(FromDegrees + Delta * FMath::Clamp(Alpha, 0.0f, 1.0f), 360.0f);
	return Result < 0.0f ? Result + 360.0f : Result;
}

FWorldWeatherFront UWorldWeatherMath::Blend(const FWorldWeatherFront& From, const FWorldWeatherFront& To, float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	FWorldWeatherFront Out;
	Out.CloudCover = FMath::Lerp(From.CloudCover, To.CloudCover, A);
	Out.CloudDensity = FMath::Lerp(From.CloudDensity, To.CloudDensity, A);
	Out.Precipitation = FMath::Lerp(From.Precipitation, To.Precipitation, A);
	Out.FogDensity = FMath::Lerp(From.FogDensity, To.FogDensity, A);
	Out.WindSpeed = FMath::Lerp(From.WindSpeed, To.WindSpeed, A);
	Out.WindAzimuth = LerpAzimuth(From.WindAzimuth, To.WindAzimuth, A);
	Out.Gustiness = FMath::Lerp(From.Gustiness, To.Gustiness, A);
	Out.ThunderRate = FMath::Lerp(From.ThunderRate, To.ThunderRate, A);
	Out.TemperatureOffset = FMath::Lerp(From.TemperatureOffset, To.TemperatureOffset, A);
	Out.Humidity = FMath::Lerp(From.Humidity, To.Humidity, A);
	return Out;
}

float UWorldWeatherMath::BlendAlpha(double StartSeconds, float BlendSeconds, double NowSeconds)
{
	if (BlendSeconds <= 0.0f)
	{
		return 1.0f;
	}
	return static_cast<float>(FMath::Clamp((NowSeconds - StartSeconds) / static_cast<double>(BlendSeconds), 0.0, 1.0));
}

FWorldWeatherFront UWorldWeatherMath::ResolveFront(const FWorldWeatherSyncPoint& Sync, double NowSeconds)
{
	return Blend(Sync.FromFront, Sync.ToFront, BlendAlpha(Sync.StartSeconds, Sync.BlendSeconds, NowSeconds));
}

FGameplayTag UWorldWeatherMath::ResolveTag(const FWorldWeatherSyncPoint& Sync, double NowSeconds)
{
	return BlendAlpha(Sync.StartSeconds, Sync.BlendSeconds, NowSeconds) >= 0.5f || !Sync.FromTag.IsValid() ? Sync.ToTag : Sync.FromTag;
}

bool UWorldWeatherMath::IsHoldOver(const FWorldWeatherSyncPoint& Sync, double NowSeconds)
{
	return NowSeconds >= Sync.StartSeconds + static_cast<double>(Sync.BlendSeconds) + static_cast<double>(Sync.HoldSeconds);
}

int32 UWorldWeatherMath::PickNext(int32 Seed, int32 Day, int32 Index, float Temperature, float Moisture, const TArray<FWorldWeatherPreset>& Presets,
	int32 PreviousIndex, float& OutHoldSeconds)
{
	OutHoldSeconds = 0.0f;
	if (Presets.Num() == 0)
	{
		return -1;
	}
	TArray<float> Weights;
	Weights.SetNumZeroed(Presets.Num());
	float Total = 0.0f;
	for (int32 i = 0; i < Presets.Num(); ++i)
	{
		Weights[i] = FMath::Max(Presets[i].WeightAt(Temperature, Moisture), 0.0f);
		Total += Weights[i];
	}
	// Never the same front twice in a row while another has weight.
	if (Presets.IsValidIndex(PreviousIndex) && Total - Weights[PreviousIndex] > KINDA_SMALL_NUMBER)
	{
		Total -= Weights[PreviousIndex];
		Weights[PreviousIndex] = 0.0f;
	}
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return -1;
	}
	const uint32 Hash = HashCombine(HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(Day)), static_cast<uint32>(Index));
	FRandomStream Stream(static_cast<int32>(Hash));
	float Draw = Stream.FRand() * Total;
	int32 Picked = Presets.Num() - 1;
	for (int32 i = 0; i < Presets.Num(); ++i)
	{
		if (Weights[i] <= 0.0f)
		{
			continue;
		}
		if (Draw < Weights[i])
		{
			Picked = i;
			break;
		}
		Draw -= Weights[i];
	}
	const FWorldWeatherPreset& P = Presets[Picked];
	const float Lo = FMath::Max(FMath::Min(P.HoldMinutesMin, P.HoldMinutesMax), 0.0f);
	const float Hi = FMath::Max(P.HoldMinutesMin, P.HoldMinutesMax);
	OutHoldSeconds = FMath::Lerp(Lo, Hi, Stream.FRand()) * 60.0f;
	return Picked;
}

float UWorldWeatherMath::TemperatureC(float Temperature01, float ColdC, float HotC, float Daylight, float NightCoolingC, float FrontOffsetC)
{
	return FMath::Lerp(ColdC, HotC, FMath::Clamp(Temperature01, 0.0f, 1.0f)) - NightCoolingC * (1.0f - FMath::Clamp(Daylight, 0.0f, 1.0f)) + FrontOffsetC;
}

// Not inlined: the test calls it with a degenerate map in the same unity file, and the compiler, folding the call, reports
// the guarded division as a divide by zero (C4723, an error in this build).
FORCENOINLINE float UWorldWeatherMath::FreezeLevel(float ColdC, float HotC, float SnowBelowC)
{
	const float Span = HotC - ColdC;
	if (FMath::Abs(Span) <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp((SnowBelowC - ColdC) / Span, 0.0f, 1.0f);
}

EWorldWeatherPrecipitation UWorldWeatherMath::PrecipitationType(float InTemperatureC, float Rate, float SnowBelowC, float SleetBandC)
{
	if (Rate <= 0.0f)
	{
		return EWorldWeatherPrecipitation::None;
	}
	if (InTemperatureC <= SnowBelowC)
	{
		return EWorldWeatherPrecipitation::Snow;
	}
	if (InTemperatureC < SnowBelowC + FMath::Max(SleetBandC, 0.0f))
	{
		return EWorldWeatherPrecipitation::Sleet;
	}
	return EWorldWeatherPrecipitation::Rain;
}

float UWorldWeatherMath::ColdFactor(float InTemperatureC, float SnowBelowC, float SleetBandC)
{
	const float Band = FMath::Max(SleetBandC, KINDA_SMALL_NUMBER);
	return FMath::Clamp(1.0f - (InTemperatureC - SnowBelowC) / Band, 0.0f, 1.0f);
}

float UWorldWeatherMath::PrecipitationRateAt(float FrontRate, float Moisture, float DryScale)
{
	return FMath::Clamp(FrontRate, 0.0f, 1.0f) * FMath::Lerp(FMath::Clamp(DryScale, 0.0f, 1.0f), 1.0f, FMath::Clamp(Moisture, 0.0f, 1.0f));
}

void UWorldWeatherMath::LocalFrame(const FVector& Up, FVector& OutNorth, FVector& OutEast)
{
	const FVector U = Up.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
	FVector North = FVector::UpVector - U * FVector::DotProduct(FVector::UpVector, U);
	if (!North.Normalize(1.0e-4))
	{
		// The up is the world's Z (a plane) or its opposite: north is +Y, so that east (north x up) is +X.
		North = FVector::RightVector - U * FVector::DotProduct(FVector::RightVector, U);
		North.Normalize();
	}
	OutNorth = North;
	OutEast = FVector::CrossProduct(North, U).GetSafeNormal();
}

void UWorldWeatherMath::OcclusionFrame(const FVector& View, const FVector& Up, float Height, float Extent,
	FVector& OutOrigin, FVector& OutForward, FVector& OutU, FVector& OutV, FVector& OutCameraUp)
{
	const FVector U = Up.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
	FVector North, East;
	LocalFrame(U, North, East);
	const float Width = 2.0f * FMath::Max(Extent, 1.0f);
	OutOrigin = View + U * Height;
	OutForward = -U;
	OutCameraUp = North;
	// The capture's right is its up crossed into its forward (FRotationMatrix::MakeFromXZ(Forward, North): Y = Z x X):
	// north x (-up) = -east. Texture U grows along the right, V along the down (-north); both over the width.
	OutU = -East / Width;
	OutV = -North / Width;
}

void UWorldWeatherMath::OcclusionCoords(const FVector& Point, const FVector& Origin, const FVector& Forward, const FVector& U, const FVector& V,
	FVector2D& OutUV, float& OutDepth)
{
	const FVector D = Point - Origin;
	OutUV = FVector2D(0.5 + FVector::DotProduct(D, U), 0.5 + FVector::DotProduct(D, V));
	OutDepth = static_cast<float>(FVector::DotProduct(D, Forward));
}

float UWorldWeatherMath::GustFactor(double Seconds, float Gustiness)
{
	const float G = FMath::Clamp(Gustiness, 0.0f, 1.0f);
	if (G <= 0.0f)
	{
		return 1.0f;
	}
	// Two slow sines of unrelated periods: a lull and a gust every few seconds, never the same twice for a while.
	const double Noise = 0.6 * FMath::Sin(Seconds * (UE_DOUBLE_TWO_PI / 7.3)) + 0.4 * FMath::Sin(Seconds * (UE_DOUBLE_TWO_PI / 2.9) + 1.0);
	return FMath::Max(1.0f + G * static_cast<float>(Noise), 0.0f);
}

FVector UWorldWeatherMath::WindVector(float Speed, float AzimuthDegrees, const FVector& Up, float Gustiness, double Seconds)
{
	FVector North, East;
	LocalFrame(Up, North, East);
	const float A = FMath::DegreesToRadians(AzimuthDegrees);
	return (North * FMath::Cos(A) + East * FMath::Sin(A)) * (FMath::Max(Speed, 0.0f) * GustFactor(Seconds, Gustiness));
}

void UWorldWeatherMath::IntegrateAccumulators(const FWorldWeatherSyncPoint& Sync, double NowSeconds, const FWorldWeatherAccumulatorRates& Rates,
	float& OutWetness, float& OutSnowCover)
{
	float W = FMath::Clamp(Sync.Wetness, 0.0f, 1.0f);
	float S = FMath::Clamp(Sync.SnowCover, 0.0f, 1.0f);
	const double Elapsed = NowSeconds - Sync.StartSeconds;
	if (Elapsed > 0.0)
	{
		using namespace WorldWeatherConstants;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Elapsed / AccumulatorStepSeconds), 1, AccumulatorMaxSteps);
		const double Dt = Elapsed / Steps;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double T = Sync.StartSeconds + (i + 0.5) * Dt;
			const float A = BlendAlpha(Sync.StartSeconds, Sync.BlendSeconds, T);
			const float P = FMath::Lerp(Sync.FromFront.Precipitation, Sync.ToFront.Precipitation, A);
			const float H = FMath::Lerp(Sync.FromFront.Humidity, Sync.ToFront.Humidity, A);
			const float DtF = static_cast<float>(Dt);
			if (P > 0.0f)
			{
				W += (1.0f - W) * P * DtF / Rates.WetnessRiseSeconds;
				S += (1.0f - S) * P * DtF / Rates.SnowRiseSeconds;
			}
			else
			{
				// Humid air dries the ground up to half as fast.
				W -= W * DtF * (1.0f - 0.5f * FMath::Clamp(H, 0.0f, 1.0f)) / Rates.DryingSeconds;
				S -= S * DtF / Rates.MeltSeconds;
			}
			W = FMath::Clamp(W, 0.0f, 1.0f);
			S = FMath::Clamp(S, 0.0f, 1.0f);
		}
	}
	OutWetness = W;
	OutSnowCover = S;
}

bool UWorldWeatherMath::StrikeInMinute(int32 Seed, int64 Minute, float ThunderRate, float& OutSecondInMinute)
{
	OutSecondInMinute = 0.0f;
	if (ThunderRate <= 0.0f)
	{
		return false;
	}
	const uint32 Hash = HashCombine(HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(Minute & 0xffffffff)), static_cast<uint32>(Minute >> 32));
	FRandomStream Stream(static_cast<int32>(Hash));
	const float Draw = Stream.FRand();
	OutSecondInMinute = Stream.FRand() * 60.0f;
	return Draw < FMath::Min(ThunderRate, 1.0f);
}

FGameplayTag UWorldWeatherMath::PrecipitationTag(EWorldWeatherPrecipitation Type)
{
	switch (Type)
	{
	case EWorldWeatherPrecipitation::Rain: return WorldWeatherTags::Weather_Precipitation_Rain;
	case EWorldWeatherPrecipitation::Snow: return WorldWeatherTags::Weather_Precipitation_Snow;
	case EWorldWeatherPrecipitation::Sleet: return WorldWeatherTags::Weather_Precipitation_Sleet;
	default: return WorldWeatherTags::Weather_Precipitation_None;
	}
}

FWorldWeatherConditions UWorldWeatherMath::Resolve(const FWorldWeatherFront& Front, const FGameplayTag& PresetTag, const FWorldWeatherClimateSample& Climate,
	float InTemperatureC, float Wetness, float SnowCover, float Exposure, double Seconds, const FWorldWeatherRules& Rules)
{
	FWorldWeatherConditions C;
	C.Front = Front;
	C.PresetTag = PresetTag;
	C.Up = Climate.Up.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
	C.TemperatureC = InTemperatureC;
	C.Exposure = FMath::Clamp(Exposure, 0.0f, 1.0f);
	C.PrecipitationRate = Climate.bUnderwater ? 0.0f : PrecipitationRateAt(Front.Precipitation, Climate.Moisture, Rules.DryPrecipitationScale);
	C.PrecipitationType = PrecipitationType(InTemperatureC, C.PrecipitationRate, Rules.SnowBelowC, Rules.SleetBandC);
	C.WindVector = WindVector(Front.WindSpeed, Front.WindAzimuth, C.Up, Front.Gustiness, Seconds);
	// The ground's memory: snow where it is cold, wet where it is not, a band between.
	const float Cold = ColdFactor(InTemperatureC, Rules.SnowBelowC, Rules.SleetBandC);
	C.SnowCover = FMath::Clamp(SnowCover, 0.0f, 1.0f) * Cold;
	C.Wetness = FMath::Clamp(Wetness, 0.0f, 1.0f) * (1.0f - Cold);

	if (PresetTag.IsValid())
	{
		C.Tags.AddTag(PresetTag);
	}
	C.Tags.AddTag(PrecipitationTag(C.PrecipitationType));
	if (C.PrecipitationType != EWorldWeatherPrecipitation::None)
	{
		C.Tags.AddTag(C.PrecipitationRate >= Rules.HeavyPrecipitationAbove ? WorldWeatherTags::Weather_Intensity_Heavy : WorldWeatherTags::Weather_Intensity_Light);
	}
	if (Front.ThunderRate > 0.0f)
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Storm);
	}
	if (Front.FogDensity >= Rules.FogTagAbove)
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Fog);
	}
	// The band from the mean speed, so a gust does not flicker the tag.
	if (Front.WindSpeed < Rules.CalmWindBelow)
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Wind_Calm);
	}
	else if (Front.WindSpeed < Rules.StrongWindAbove)
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Wind_Breeze);
	}
	else
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Wind_Strong);
	}
	if (C.Exposure < Rules.ShelteredBelow)
	{
		C.Tags.AddTag(WorldWeatherTags::Weather_Sheltered);
	}
	return C;
}
