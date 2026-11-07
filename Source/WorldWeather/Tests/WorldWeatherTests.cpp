// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "WorldWeatherAmbienceComponent.h"
#include "WorldWeatherLensComponent.h"
#include "WorldWeatherLightningComponent.h"
#include "WorldWeatherMath.h"
#include "WorldWeatherPrecipitationComponent.h"
#include "Components/AudioComponent.h"
#include "WorldWeatherPreset.h"
#include "WorldWeatherSettings.h"
#include "WorldWeatherSubsystem.h"
#include "WorldWeatherTags.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherViewer.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "WorldClockSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#define WORLDWEATHER_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FWorldWeatherFront MakeFront(float Cover, float Precip, float Fog, float Wind, float Azimuth, float Gust, float Thunder, float Temp, float Humidity)
	{
		FWorldWeatherFront F;
		F.CloudCover = Cover;
		F.CloudDensity = Cover;
		F.Precipitation = Precip;
		F.FogDensity = Fog;
		F.WindSpeed = Wind;
		F.WindAzimuth = Azimuth;
		F.Gustiness = Gust;
		F.ThunderRate = Thunder;
		F.TemperatureOffset = Temp;
		F.Humidity = Humidity;
		return F;
	}

	FWorldWeatherSyncPoint MakeSync(const FWorldWeatherFront& From, const FWorldWeatherFront& To, double Start, float Blend, float Hold)
	{
		FWorldWeatherSyncPoint S;
		S.FromFront = From;
		S.ToFront = To;
		S.FromTag = WorldWeatherTags::Weather_Preset_Clear;
		S.ToTag = WorldWeatherTags::Weather_Preset_Storm;
		S.StartSeconds = Start;
		S.BlendSeconds = Blend;
		S.HoldSeconds = Hold;
		S.Serial = 1;
		return S;
	}
}

// ---------------------------------------------------------------------------
// The blend: member by member, the azimuth the short way round.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherBlendTest, "WorldWeather.Math.Blend", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherBlendTest::RunTest(const FString& Parameters)
{
	const FWorldWeatherFront Clear = MakeFront(0.1f, 0.0f, 0.0f, 100.0f, 350.0f, 0.2f, 0.0f, 2.0f, 0.2f);
	const FWorldWeatherFront Storm = MakeFront(1.0f, 1.0f, 0.3f, 900.0f, 20.0f, 0.8f, 0.5f, -4.0f, 1.0f);
	const FWorldWeatherFront Mid = UWorldWeatherMath::Blend(Clear, Storm, 0.5f);
	TestEqual(TEXT("Cloud cover half way"), Mid.CloudCover, 0.55f, 1.0e-5f);
	TestEqual(TEXT("Precipitation half way"), Mid.Precipitation, 0.5f, 1.0e-5f);
	TestEqual(TEXT("Wind speed half way"), Mid.WindSpeed, 500.0f, 1.0e-3f);
	TestEqual(TEXT("Thunder half way"), Mid.ThunderRate, 0.25f, 1.0e-5f);
	TestEqual(TEXT("Temperature offset half way"), Mid.TemperatureOffset, -1.0f, 1.0e-5f);
	TestEqual(TEXT("Azimuth 350 -> 20 goes through north: 5"), Mid.WindAzimuth, 5.0f, 1.0e-3f);
	TestEqual(TEXT("Azimuth 20 -> 350 back through north: 5"), UWorldWeatherMath::LerpAzimuth(20.0f, 350.0f, 0.5f), 5.0f, 1.0e-3f);
	TestEqual(TEXT("Azimuth 90 -> 270 half way is 180"), UWorldWeatherMath::LerpAzimuth(90.0f, 270.0f, 0.5f), 180.0f, 1.0e-3f);
	TestTrue(TEXT("Alpha 0 is From"), UWorldWeatherMath::Blend(Clear, Storm, 0.0f).WindAzimuth == 350.0f);
	TestTrue(TEXT("Alpha 1 is To"), UWorldWeatherMath::Blend(Clear, Storm, 1.0f).WindAzimuth == 20.0f);
	TestTrue(TEXT("Alpha clamps"), UWorldWeatherMath::Blend(Clear, Storm, 3.0f).CloudCover == 1.0f);
	TestEqual(TEXT("A zero blend is done at once"), UWorldWeatherMath::BlendAlpha(100.0, 0.0f, 100.0), 1.0f);
	TestEqual(TEXT("Before the start the blend is 0"), UWorldWeatherMath::BlendAlpha(100.0, 50.0f, 90.0), 0.0f);
	TestEqual(TEXT("Half way"), UWorldWeatherMath::BlendAlpha(100.0, 50.0f, 125.0), 0.5f, 1.0e-6f);
	return true;
}

// ---------------------------------------------------------------------------
// The schedule: the same seed, day and climate give the same sequence; affinities zero a preset out; no immediate repeat.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherScheduleTest, "WorldWeather.Math.Schedule", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherScheduleTest::RunTest(const FString& Parameters)
{
	TArray<FWorldWeatherPreset> Presets;
	UWorldWeatherPresetTable::FillDefaults(Presets);
	TestEqual(TEXT("Seven defaults"), Presets.Num(), 7);
	{
		TSet<FGameplayTag> Tags;
		for (const FWorldWeatherPreset& P : Presets)
		{
			TestTrue(TEXT("A valid preset tag"), P.Tag.IsValid() && P.Tag.MatchesTag(WorldWeatherTags::Weather_Preset));
			Tags.Add(P.Tag);
		}
		TestEqual(TEXT("Every tag once"), Tags.Num(), 7);
	}
	const int32 SnowIndex = Presets.IndexOfByPredicate([](const FWorldWeatherPreset& P) { return P.Tag == WorldWeatherTags::Weather_Preset_Snow; });
	const int32 RainIndex = Presets.IndexOfByPredicate([](const FWorldWeatherPreset& P) { return P.Tag == WorldWeatherTags::Weather_Preset_Rain; });

	// Determinism: two runs of the same inputs agree, a different seed differs somewhere.
	TArray<int32> A, B, C;
	TArray<float> HoldsA, HoldsB;
	int32 PrevA = -1, PrevB = -1, PrevC = -1;
	for (int32 i = 0; i < 40; ++i)
	{
		float Hold = 0.0f;
		PrevA = UWorldWeatherMath::PickNext(1234, 3, i, 0.5f, 0.5f, Presets, PrevA, Hold);
		A.Add(PrevA);
		HoldsA.Add(Hold);
		PrevB = UWorldWeatherMath::PickNext(1234, 3, i, 0.5f, 0.5f, Presets, PrevB, Hold);
		B.Add(PrevB);
		HoldsB.Add(Hold);
		PrevC = UWorldWeatherMath::PickNext(99, 3, i, 0.5f, 0.5f, Presets, PrevC, Hold);
		C.Add(PrevC);
	}
	TestTrue(TEXT("The same seed, day and climate give the same fronts"), A == B && HoldsA == HoldsB);
	TestTrue(TEXT("Another seed gives other fronts"), A != C);
	for (int32 i = 1; i < A.Num(); ++i)
	{
		TestTrue(TEXT("No front twice in a row"), A[i] != A[i - 1]);
		TestTrue(TEXT("A valid index"), Presets.IsValidIndex(A[i]));
		const FWorldWeatherPreset& P = Presets[A[i]];
		TestTrue(TEXT("The hold within the preset's range"), HoldsA[i] >= P.HoldMinutesMin * 60.0f - 1.0e-3f && HoldsA[i] <= P.HoldMinutesMax * 60.0f + 1.0e-3f);
	}

	// Affinities: a hot desert never draws Snow (HotWeight 0); a cold wet place draws Snow more than Rain.
	int32 SnowInDesert = 0, SnowInTundra = 0, RainInTundra = 0;
	int32 Prev = -1;
	for (int32 i = 0; i < 400; ++i)
	{
		float Hold = 0.0f;
		Prev = UWorldWeatherMath::PickNext(7, 1, i, 1.0f, 0.0f, Presets, Prev, Hold);
		SnowInDesert += Prev == SnowIndex ? 1 : 0;
	}
	Prev = -1;
	for (int32 i = 0; i < 400; ++i)
	{
		float Hold = 0.0f;
		Prev = UWorldWeatherMath::PickNext(7, 1, i, 0.0f, 1.0f, Presets, Prev, Hold);
		SnowInTundra += Prev == SnowIndex ? 1 : 0;
		RainInTundra += Prev == RainIndex ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("400 draws: snow in the desert %d, in the tundra %d (rain there %d)"), SnowInDesert, SnowInTundra, RainInTundra));
	TestEqual(TEXT("No snow in a hot desert"), SnowInDesert, 0);
	TestTrue(TEXT("Snow over rain in the tundra"), SnowInTundra > RainInTundra && SnowInTundra > 40);

	// A table with weight nowhere, and an empty one.
	TArray<FWorldWeatherPreset> Weightless;
	Weightless.AddDefaulted();
	Weightless[0].ColdWeight = Weightless[0].TemperateWeight = Weightless[0].HotWeight = 0.0f;
	float Hold = 0.0f;
	TestEqual(TEXT("No weight anywhere: -1"), UWorldWeatherMath::PickNext(1, 1, 0, 0.5f, 0.5f, Weightless, -1, Hold), -1);
	TestEqual(TEXT("An empty table: -1"), UWorldWeatherMath::PickNext(1, 1, 0, 0.5f, 0.5f, TArray<FWorldWeatherPreset>(), -1, Hold), -1);
	// One preset only: it repeats (nothing else has weight).
	TArray<FWorldWeatherPreset> One;
	One.AddDefaulted();
	TestEqual(TEXT("One preset repeats"), UWorldWeatherMath::PickNext(1, 1, 0, 0.5f, 0.5f, One, 0, Hold), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Precipitation type by the temperature, the sleet band, the moisture factor, the cold factor.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherPrecipitationTypeTest, "WorldWeather.Math.PrecipitationType", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherPrecipitationTypeTest::RunTest(const FString& Parameters)
{
	// The freeze level (P3): SnowBelowC inverted through the linear map; clamped; 0 for a degenerate map.
	TestEqual(TEXT("Freeze level: -20..35 C, snow at 0 C"), UWorldWeatherMath::FreezeLevel(-20.0f, 35.0f, 0.0f), 20.0f / 55.0f, 1.0e-6f);
	TestEqual(TEXT("Freeze level under the cold end clamps to 0"), UWorldWeatherMath::FreezeLevel(5.0f, 35.0f, 0.0f), 0.0f);
	TestEqual(TEXT("Freeze level over the hot end clamps to 1"), UWorldWeatherMath::FreezeLevel(-20.0f, -5.0f, 0.0f), 1.0f);
	TestEqual(TEXT("A degenerate map gives 0"), UWorldWeatherMath::FreezeLevel(10.0f, 10.0f, 0.0f), 0.0f);
	TestTrue(TEXT("No rate: nothing"), UWorldWeatherMath::PrecipitationType(10.0f, 0.0f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::None);
	TestTrue(TEXT("Warm: rain"), UWorldWeatherMath::PrecipitationType(10.0f, 0.5f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::Rain);
	TestTrue(TEXT("Freezing: snow"), UWorldWeatherMath::PrecipitationType(0.0f, 0.5f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::Snow);
	TestTrue(TEXT("Below freezing: snow"), UWorldWeatherMath::PrecipitationType(-12.0f, 0.5f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::Snow);
	TestTrue(TEXT("Just above freezing: sleet"), UWorldWeatherMath::PrecipitationType(1.0f, 0.5f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::Sleet);
	TestTrue(TEXT("At the band's top: rain"), UWorldWeatherMath::PrecipitationType(2.0f, 0.5f, 0.0f, 2.0f) == EWorldWeatherPrecipitation::Rain);
	TestTrue(TEXT("No sleet band: rain just above freezing"), UWorldWeatherMath::PrecipitationType(0.5f, 0.5f, 0.0f, 0.0f) == EWorldWeatherPrecipitation::Rain);

	TestEqual(TEXT("Cold factor 1 at freezing"), UWorldWeatherMath::ColdFactor(0.0f, 0.0f, 2.0f), 1.0f);
	TestEqual(TEXT("Cold factor half in the band"), UWorldWeatherMath::ColdFactor(1.0f, 0.0f, 2.0f), 0.5f, 1.0e-6f);
	TestEqual(TEXT("Cold factor 0 above the band"), UWorldWeatherMath::ColdFactor(5.0f, 0.0f, 2.0f), 0.0f);

	TestEqual(TEXT("Dry place: a quarter of the front's rate"), UWorldWeatherMath::PrecipitationRateAt(0.8f, 0.0f, 0.25f), 0.2f, 1.0e-6f);
	TestEqual(TEXT("Wet place: the front's rate"), UWorldWeatherMath::PrecipitationRateAt(0.8f, 1.0f, 0.25f), 0.8f, 1.0e-6f);

	// Degrees: the linear map, the night's cooling, the front's offset.
	TestEqual(TEXT("Noon at the hot end"), UWorldWeatherMath::TemperatureC(1.0f, -20.0f, 35.0f, 1.0f, 6.0f, 0.0f), 35.0f, 1.0e-4f);
	TestEqual(TEXT("Midnight at the cold end"), UWorldWeatherMath::TemperatureC(0.0f, -20.0f, 35.0f, 0.0f, 6.0f, 0.0f), -26.0f, 1.0e-4f);
	TestEqual(TEXT("A cold front at noon in the middle"), UWorldWeatherMath::TemperatureC(0.5f, -20.0f, 35.0f, 1.0f, 6.0f, -4.0f), 3.5f, 1.0e-4f);
	return true;
}

// ---------------------------------------------------------------------------
// The wind: tangent to the up on a planet, the plane's XY, the azimuth convention, the gust within its band.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherWindVectorTest, "WorldWeather.Math.WindVector", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherWindVectorTest::RunTest(const FString& Parameters)
{
	// A plane: azimuth 0 is +Y (north), 90 is +X (east, where the clock's sun rises by default), in the XY plane.
	const FVector North = UWorldWeatherMath::WindVector(300.0f, 0.0f, FVector::UpVector, 0.0f, 0.0);
	TestTrue(TEXT("Plane: azimuth 0 blows along +Y at the speed"), North.Equals(FVector(0.0, 300.0, 0.0), 1.0e-3));
	const FVector East = UWorldWeatherMath::WindVector(300.0f, 90.0f, FVector::UpVector, 0.0f, 0.0);
	TestTrue(TEXT("Plane: azimuth 90 blows along +X"), East.Equals(FVector(300.0, 0.0, 0.0), 1.0e-3));
	// The frame is cyclic with the world's axes on the plane and on the planet: east x north = up.
	{
		FVector N, E;
		UWorldWeatherMath::LocalFrame(FVector::UpVector, N, E);
		TestTrue(TEXT("Plane: east x north = up"), FVector::CrossProduct(E, N).Equals(FVector::UpVector, 1.0e-6));
		UWorldWeatherMath::LocalFrame(FVector(0.6, 0.0, 0.8), N, E);
		TestTrue(TEXT("Planet: east x north = up"), FVector::CrossProduct(E, N).Equals(FVector(0.6, 0.0, 0.8), 1.0e-6));
	}
	TestTrue(TEXT("Plane: no vertical wind"), FMath::IsNearlyZero(UWorldWeatherMath::WindVector(300.0f, 215.0f, FVector::UpVector, 0.0f, 0.0).Z, 1.0e-6));

	// A planet: tangent to the local up everywhere, north toward the pole.
	double WorstTangent = 0.0;
	for (const FVector& Up : { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0.6, 0.0, 0.8), FVector(-0.3, 0.5, -0.4).GetSafeNormal(), FVector(0.0, 0.0, -1.0) })
	{
		for (const float Azimuth : { 0.0f, 45.0f, 137.0f, 270.0f })
		{
			const FVector W = UWorldWeatherMath::WindVector(500.0f, Azimuth, Up, 0.0f, 0.0);
			WorstTangent = FMath::Max(WorstTangent, FMath::Abs(FVector::DotProduct(W.GetSafeNormal(), Up.GetSafeNormal())));
			TestTrue(TEXT("Planet: the speed kept"), FMath::IsNearlyEqual(W.Size(), 500.0, 1.0e-3));
		}
	}
	AddInfo(FString::Printf(TEXT("Worst wind component along the up: %.2e"), WorstTangent));
	TestTrue(TEXT("Planet: the wind is tangent to the up"), WorstTangent < 1.0e-5);
	const FVector OnEquator = UWorldWeatherMath::WindVector(100.0f, 0.0f, FVector(1, 0, 0), 0.0f, 0.0);
	TestTrue(TEXT("Planet: north on the equator points to the pole"), OnEquator.Equals(FVector(0.0, 0.0, 100.0), 1.0e-3));
	const FVector EastOnEquator = UWorldWeatherMath::WindVector(100.0f, 90.0f, FVector(1, 0, 0), 0.0f, 0.0);
	TestTrue(TEXT("Planet: east on the equator at longitude 0 is +Y"), EastOnEquator.Equals(FVector(0.0, 100.0, 0.0), 1.0e-3));

	// The gust: 1 without gustiness; within 1 +- G and never negative with it; the mean near 1.
	TestEqual(TEXT("No gustiness: 1"), UWorldWeatherMath::GustFactor(123.4, 0.0f), 1.0f);
	float Lo = 10.0f, Hi = 0.0f;
	double Sum = 0.0;
	const int32 N = 20000;
	for (int32 i = 0; i < N; ++i)
	{
		const float G = UWorldWeatherMath::GustFactor(i * 0.05, 0.8f);
		Lo = FMath::Min(Lo, G);
		Hi = FMath::Max(Hi, G);
		Sum += G;
	}
	AddInfo(FString::Printf(TEXT("Gust factor at 0.8: %.3f .. %.3f, mean %.4f"), Lo, Hi, Sum / N));
	TestTrue(TEXT("Gust within the band"), Lo >= 1.0f - 0.8f - 1.0e-4f && Hi <= 1.0f + 0.8f + 1.0e-4f && Lo >= 0.0f);
	TestTrue(TEXT("Gust reaches most of the band"), Hi > 1.5f && Lo < 0.5f);
	TestTrue(TEXT("Gust mean near 1"), FMath::Abs(Sum / N - 1.0) < 0.02);
	return true;
}

// ---------------------------------------------------------------------------
// The accumulators: wetness rises under rain and decays after; snow follows; integrating from a sync point equals ticking
// (the same inputs, the same steps, on any machine); the local reading is snow where it is cold.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherAccumulatorsTest, "WorldWeather.Math.Accumulators", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherAccumulatorsTest::RunTest(const FString& Parameters)
{
	FWorldWeatherAccumulatorRates Rates;
	Rates.WetnessRiseSeconds = 600.0f;
	Rates.DryingSeconds = 3600.0f;
	Rates.SnowRiseSeconds = 1800.0f;
	Rates.MeltSeconds = 14400.0f;
	const FWorldWeatherFront Dry = MakeFront(0.1f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f);
	const FWorldWeatherFront Rain = MakeFront(0.9f, 1.0f, 0.2f, 300.0f, 0.0f, 0.0f, 0.0f, -3.0f, 0.9f);

	// Rain from dry ground: wetness climbs toward 1, most of the way in one rise time; snow slower.
	const FWorldWeatherSyncPoint Raining = MakeSync(Rain, Rain, 1000.0, 0.0f, 7200.0f);
	float W = 0.0f, S = 0.0f;
	UWorldWeatherMath::IntegrateAccumulators(Raining, 1000.0, Rates, W, S);
	TestEqual(TEXT("At the start: the sync point's values"), W, 0.0f);
	UWorldWeatherMath::IntegrateAccumulators(Raining, 1600.0, Rates, W, S);
	AddInfo(FString::Printf(TEXT("After 600 s of rain: wetness %.3f, snow %.3f"), W, S));
	TestTrue(TEXT("Wet after one rise time"), W > 0.6f && W < 0.7f);
	TestTrue(TEXT("Snow follows, slower"), S > 0.25f && S < W);
	float W2 = 0.0f, S2 = 0.0f;
	UWorldWeatherMath::IntegrateAccumulators(Raining, 1000.0 + 3600.0 * 4.0, Rates, W2, S2);
	TestTrue(TEXT("Hours of rain: soaked"), W2 > 0.99f && S2 > 0.99f);

	// Then dry air from soaked ground: wetness decays; snow lingers.
	FWorldWeatherSyncPoint Drying = MakeSync(Dry, Dry, 5000.0, 0.0f, 7200.0f);
	Drying.Wetness = 1.0f;
	Drying.SnowCover = 1.0f;
	UWorldWeatherMath::IntegrateAccumulators(Drying, 5000.0 + 3600.0, Rates, W, S);
	AddInfo(FString::Printf(TEXT("After 3600 s of dry air: wetness %.3f, snow %.3f"), W, S));
	TestTrue(TEXT("Dries toward e^-1 in one drying time (humidity 0.2 slows it a little)"), W > 0.33f && W < 0.45f);
	TestTrue(TEXT("Snow goes slower"), S > 0.75f && S < 0.8f);

	// A blend from dry to rain: between the two.
	const FWorldWeatherSyncPoint Blending = MakeSync(Dry, Rain, 0.0, 1200.0f, 3600.0f);
	UWorldWeatherMath::IntegrateAccumulators(Blending, 1200.0, Rates, W, S);
	float WFull = 0.0f, SFull = 0.0f;
	UWorldWeatherMath::IntegrateAccumulators(Raining, 1000.0 + 1200.0, Rates, WFull, SFull);
	TestTrue(TEXT("A blend into rain wets less than rain from the start"), W > 0.2f && W < WFull);

	// Integrating from the sync point in one go equals ticking from it in pieces (the step is a game minute either way).
	float WTick = 0.0f, STick = 0.0f;
	{
		FWorldWeatherSyncPoint Walking = Blending;
		for (double T = 0.0; T < 1200.0; T += 60.0)
		{
			UWorldWeatherMath::IntegrateAccumulators(Walking, T + 60.0, Rates, WTick, STick);
			Walking.Wetness = WTick;
			Walking.SnowCover = STick;
			Walking.StartSeconds = T + 60.0;
			// The blend goes on from the original start: keep the fronts' alpha by moving From to the present.
			Walking.FromFront = UWorldWeatherMath::ResolveFront(Blending, T + 60.0);
			Walking.BlendSeconds = static_cast<float>(FMath::Max(1200.0 - (T + 60.0), 0.0));
		}
	}
	AddInfo(FString::Printf(TEXT("Blend 1200 s: in one go wetness %.5f, ticked %.5f"), W, WTick));
	TestTrue(TEXT("One go equals ticking"), FMath::IsNearlyEqual(W, WTick, 2.0e-3f) && FMath::IsNearlyEqual(S, STick, 2.0e-3f));

	// Long holds stay cheap and smooth: a day of rain in at most 256 steps still soaks the ground.
	UWorldWeatherMath::IntegrateAccumulators(Raining, 1000.0 + 86400.0, Rates, W, S);
	TestTrue(TEXT("A day of rain: soaked"), W > 0.99f && S > 0.99f);

	// The local reading: snow where it is cold, wet where it is warm, through the resolution.
	FWorldWeatherRules Rules;
	FWorldWeatherClimateSample Climate;
	const FWorldWeatherConditions Cold = UWorldWeatherMath::Resolve(Rain, WorldWeatherTags::Weather_Preset_Rain, Climate, -5.0f, 0.8f, 0.6f, 1.0f, 0.0, Rules);
	TestTrue(TEXT("Cold: snow falls"), Cold.PrecipitationType == EWorldWeatherPrecipitation::Snow && Cold.Tags.HasTagExact(WorldWeatherTags::Weather_Precipitation_Snow));
	TestTrue(TEXT("Cold: the cover is snow, the ground not wet"), FMath::IsNearlyEqual(Cold.SnowCover, 0.6f) && Cold.Wetness == 0.0f);
	const FWorldWeatherConditions Warm = UWorldWeatherMath::Resolve(Rain, WorldWeatherTags::Weather_Preset_Rain, Climate, 15.0f, 0.8f, 0.6f, 1.0f, 0.0, Rules);
	TestTrue(TEXT("Warm: rain falls"), Warm.PrecipitationType == EWorldWeatherPrecipitation::Rain && Warm.Tags.HasTagExact(WorldWeatherTags::Weather_Precipitation_Rain));
	TestTrue(TEXT("Warm: the ground is wet, no snow"), FMath::IsNearlyEqual(Warm.Wetness, 0.8f) && Warm.SnowCover == 0.0f);
	TestTrue(TEXT("Heavy rain tagged"), Warm.Tags.HasTagExact(WorldWeatherTags::Weather_Intensity_Heavy) && Warm.Tags.HasTagExact(WorldWeatherTags::Weather_Preset_Rain));
	TestTrue(TEXT("A breeze tagged"), Warm.Tags.HasTagExact(WorldWeatherTags::Weather_Wind_Breeze) && !Warm.Tags.HasTag(WorldWeatherTags::Weather_Storm));
	Climate.bUnderwater = true;
	const FWorldWeatherConditions Under = UWorldWeatherMath::Resolve(Rain, WorldWeatherTags::Weather_Preset_Rain, Climate, 15.0f, 0.8f, 0.6f, 0.0f, 0.0, Rules);
	TestTrue(TEXT("Underwater: nothing falls, sheltered"), Under.PrecipitationType == EWorldWeatherPrecipitation::None && Under.Tags.HasTagExact(WorldWeatherTags::Weather_Sheltered));
	return true;
}

// ---------------------------------------------------------------------------
// Lightning: the strike hash's rate over many minutes, deterministic, none at rate 0.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherLightningTest, "WorldWeather.Math.Lightning", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherLightningTest::RunTest(const FString& Parameters)
{
	int32 Strikes = 0;
	const int32 Minutes = 20000;
	for (int64 M = 0; M < Minutes; ++M)
	{
		float Second = 0.0f;
		if (UWorldWeatherMath::StrikeInMinute(42, M, 0.3f, Second))
		{
			++Strikes;
			TestTrue(TEXT("The strike's second within the minute"), Second >= 0.0f && Second < 60.0f);
		}
	}
	AddInfo(FString::Printf(TEXT("Rate 0.3 over %d minutes: %d strikes (%.3f a minute)"), Minutes, Strikes, static_cast<float>(Strikes) / Minutes));
	TestTrue(TEXT("The rate holds within 5 percent"), FMath::Abs(static_cast<float>(Strikes) / Minutes - 0.3f) < 0.015f);
	float S1 = 0.0f, S2 = 0.0f;
	const bool A = UWorldWeatherMath::StrikeInMinute(42, 777, 0.5f, S1);
	const bool B = UWorldWeatherMath::StrikeInMinute(42, 777, 0.5f, S2);
	TestTrue(TEXT("The same minute and seed strike the same"), A == B && S1 == S2);
	TestFalse(TEXT("Rate 0 never strikes"), UWorldWeatherMath::StrikeInMinute(42, 777, 0.0f, S1));
	int32 Every = 0;
	for (int64 M = 0; M < 100; ++M)
	{
		Every += UWorldWeatherMath::StrikeInMinute(42, M, 1.0f, S1) ? 1 : 0;
	}
	TestEqual(TEXT("Rate 1 strikes every minute"), Every, 100);
	return true;
}

// ---------------------------------------------------------------------------
// A sync point and a clock time give the front, the tag and whether the hold is over.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherSyncResolveTest, "WorldWeather.Sync.Resolve", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherSyncResolveTest::RunTest(const FString& Parameters)
{
	const FWorldWeatherFront Clear = MakeFront(0.1f, 0.0f, 0.0f, 100.0f, 0.0f, 0.2f, 0.0f, 2.0f, 0.2f);
	const FWorldWeatherFront Storm = MakeFront(1.0f, 1.0f, 0.3f, 900.0f, 90.0f, 0.8f, 0.5f, -4.0f, 1.0f);
	const FWorldWeatherSyncPoint Sync = MakeSync(Clear, Storm, 3600.0, 600.0f, 1800.0f);
	TestTrue(TEXT("Before the start: the front left"), UWorldWeatherMath::ResolveFront(Sync, 3000.0).CloudCover == 0.1f);
	TestTrue(TEXT("Before the start: its tag"), UWorldWeatherMath::ResolveTag(Sync, 3000.0) == WorldWeatherTags::Weather_Preset_Clear);
	const FWorldWeatherFront Quarter = UWorldWeatherMath::ResolveFront(Sync, 3750.0);
	TestEqual(TEXT("A quarter in: a quarter of the way"), Quarter.Precipitation, 0.25f, 1.0e-5f);
	TestTrue(TEXT("A quarter in: still the old tag"), UWorldWeatherMath::ResolveTag(Sync, 3750.0) == WorldWeatherTags::Weather_Preset_Clear);
	TestTrue(TEXT("Half way: the new tag"), UWorldWeatherMath::ResolveTag(Sync, 3900.0) == WorldWeatherTags::Weather_Preset_Storm);
	TestTrue(TEXT("After the blend: the front arrived at"), UWorldWeatherMath::ResolveFront(Sync, 5000.0).WindSpeed == 900.0f);
	TestFalse(TEXT("During the hold: not over"), UWorldWeatherMath::IsHoldOver(Sync, 5999.0));
	TestTrue(TEXT("At the hold's end: over"), UWorldWeatherMath::IsHoldOver(Sync, 6000.0));
	FWorldWeatherSyncPoint Empty;
	TestFalse(TEXT("No publish yet: not valid"), Empty.IsValid());
	return true;
}

// ---------------------------------------------------------------------------
// The subsystem in a game world without a provider: Z up, the settings' temperature, exposure 1; SetWeather, the tags; the
// export round trip; setters refused nowhere (standalone is the authority).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherSubsystemDefaultsTest, "WorldWeather.Subsystem.Defaults", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherSubsystemDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldWeatherDefaultsTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UWorldWeatherSubsystem* Weather = World->GetSubsystem<UWorldWeatherSubsystem>();
	UWorldClockSubsystem* Clock = World->GetSubsystem<UWorldClockSubsystem>();
	if (TestNotNull(TEXT("The game world has weather"), Weather) && TestNotNull(TEXT("... and a clock"), Clock))
	{
		const UWorldWeatherSettings* Settings = UWorldWeatherSettings::Get();
		TestNotNull(TEXT("A preset table (the defaults)"), Weather->GetPresetTable());
		TestEqual(TEXT("Seven presets"), Weather->GetPresetTable() ? Weather->GetPresetTable()->Presets.Num() : 0, 7);
		TestFalse(TEXT("No weather before the first draw"), Weather->HasWeather());

		const FVector Here(12345.0, -6789.0, 250.0);
		const FWorldWeatherClimateSample Climate = Weather->GetClimateAt(Here);
		TestTrue(TEXT("No provider: Z up"), Climate.Up.Equals(FVector::UpVector));
		TestEqual(TEXT("No provider: the settings' temperature"), Climate.Temperature, Settings->DefaultTemperature);
		TestEqual(TEXT("No provider: exposure 1 (nothing to hit)"), Weather->GetExposureAt(Here), 1.0f);

		Clock->SetTimeOfDay(12.0f);
		const double Noon = Weather->GetClockSeconds();
		TestTrue(TEXT("The clock's seconds: day 1 noon"), FMath::IsNearlyEqual(Noon, 12.0 * 3600.0, 1.0));

		TestTrue(TEXT("SetWeather(Storm)"), Weather->SetWeather(WorldWeatherTags::Weather_Preset_Storm, 0.0f));
		TestTrue(TEXT("Weather now"), Weather->HasWeather());
		TestTrue(TEXT("The present preset"), Weather->GetCurrentPreset() == WorldWeatherTags::Weather_Preset_Storm);
		TestEqual(TEXT("A zero blend is done"), Weather->GetBlendAlpha(), 1.0f);
		const FWorldWeatherConditions C = Weather->GetConditionsAt(Here);
		const float ExpectedC = UWorldWeatherMath::TemperatureC(Settings->DefaultTemperature, Settings->ColdTemperatureC, Settings->HotTemperatureC, Clock->GetDaylight(),
			Settings->NightCoolingC, C.Front.TemperatureOffset);
		AddInfo(FString::Printf(TEXT("Storm at noon: %.1f C, %s, tags %s"), C.TemperatureC, *UEnum::GetValueAsString(C.PrecipitationType), *C.Tags.ToStringSimple()));
		TestEqual(TEXT("The temperature from the settings and the front"), C.TemperatureC, ExpectedC, 1.0e-3f);
		TestTrue(TEXT("Rain in a storm at the default temperature"), C.PrecipitationType == EWorldWeatherPrecipitation::Rain);
		TestTrue(TEXT("Storm tags"), C.Tags.HasTagExact(WorldWeatherTags::Weather_Storm) && C.Tags.HasTagExact(WorldWeatherTags::Weather_Wind_Strong)
			&& C.Tags.HasTagExact(WorldWeatherTags::Weather_Preset_Storm) && C.Tags.HasTagExact(WorldWeatherTags::Weather_Intensity_Heavy));
		TestTrue(TEXT("The wind in the XY plane"), FMath::IsNearlyZero(C.WindVector.Z, 1.0e-4) && C.WindVector.Size() > 100.0);
		AddExpectedMessage(TEXT("is not a row of the preset table"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("A wrong tag is refused"), Weather->SetWeather(FGameplayTag::RequestGameplayTag(TEXT("Weather.Precipitation.Rain")), 0.0f));

		// A blend half way: the tag flips, the front is between.
		Weather->SetWeather(WorldWeatherTags::Weather_Preset_Clear, 600.0f);
		TestTrue(TEXT("Blending: still the storm"), Weather->GetCurrentPreset() == WorldWeatherTags::Weather_Preset_Storm);
		Clock->SetTimeOfDay(12.1f); // 360 s in: alpha 0.6
		TestTrue(TEXT("Past half way: clear"), Weather->GetCurrentPreset() == WorldWeatherTags::Weather_Preset_Clear);
		TestEqual(TEXT("Alpha 0.6"), Weather->GetBlendAlpha(), 0.6f, 2.0e-3f);
		const FWorldWeatherFront Mid = Weather->GetCurrentFront();
		TestTrue(TEXT("The front between"), Mid.Precipitation > 0.3f && Mid.Precipitation < 0.5f);

		// The schedule's own draw, and the lock.
		Weather->SkipToNext();
		TestTrue(TEXT("SkipToNext draws a front"), Weather->GetSyncPoint().ToTag.IsValid() && Weather->GetSyncPoint().Serial >= 3);
		TestFalse(TEXT("Not locked"), Weather->IsLocked());
		Weather->SetLocked(true);
		TestTrue(TEXT("Locked"), Weather->IsLocked());

		// Export and import round trip.
		const FWorldWeatherSaveState Saved = Weather->ExportState();
		TestTrue(TEXT("Exported the sync point"), Saved.Sync.Serial == Weather->GetSyncPoint().Serial && Saved.bLocked);
		Weather->SetLocked(false);
		Weather->SetWeather(WorldWeatherTags::Weather_Preset_Fog, 0.0f);
		Weather->ImportState(Saved);
		const FWorldWeatherSyncPoint Back = Weather->GetSyncPoint();
		TestTrue(TEXT("Imported: the sync point back"), Back.Serial == Saved.Sync.Serial && Back.ToTag == Saved.Sync.ToTag && Back.StartSeconds == Saved.Sync.StartSeconds
			&& Back.FromFront.WindSpeed == Saved.Sync.FromFront.WindSpeed);
		TestTrue(TEXT("Imported: locked again"), Weather->IsLocked());
		TestEqual(TEXT("Imported: the schedule's place"), Weather->ExportState().ScheduleIndex, Saved.ScheduleIndex);

		// The wind setter keeps the preset.
		Weather->SetWind(270.0f, 400.0f);
		const FWorldWeatherSyncPoint Wind = Weather->GetSyncPoint();
		TestTrue(TEXT("SetWind: the front's wind"), Wind.ToFront.WindAzimuth == 270.0f && Wind.ToFront.WindSpeed == 400.0f && Wind.ToTag == Back.ToTag);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The occlusion capture's frame (P2, D63): the capture sits above the view along the up and looks down it; a point
// under the origin is the texture's centre at the capture's height; east is -U, north is -V; the same on a planet's up.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherOcclusionFrameTest, "WorldWeather.Math.Occlusion", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherOcclusionFrameTest::RunTest(const FString& Parameters)
{
	// A plane: up +Z, north +Y, east +X.
	{
		const FVector View(1000.0, 2000.0, 300.0);
		FVector Origin, Forward, U, V, CameraUp;
		UWorldWeatherMath::OcclusionFrame(View, FVector::UpVector, 5000.0f, 10000.0f, Origin, Forward, U, V, CameraUp);
		TestTrue(TEXT("The origin is the height above the view"), Origin.Equals(View + FVector(0.0, 0.0, 5000.0)));
		TestTrue(TEXT("Forward is down"), Forward.Equals(-FVector::UpVector));
		TestTrue(TEXT("The camera's up is north"), CameraUp.Equals(FVector::RightVector));
		FVector2D UV;
		float Depth = 0.0f;
		UWorldWeatherMath::OcclusionCoords(View, Origin, Forward, U, V, UV, Depth);
		TestTrue(TEXT("The view is the centre"), UV.Equals(FVector2D(0.5, 0.5), 1.0e-6));
		TestEqual(TEXT("... at the capture's height"), Depth, 5000.0f);
		UWorldWeatherMath::OcclusionCoords(View + FVector(10000.0, 0.0, -1000.0), Origin, Forward, U, V, UV, Depth);
		TestTrue(TEXT("The east edge is U 0 (the capture's right is west)"), UV.Equals(FVector2D(0.0, 0.5), 1.0e-6));
		TestEqual(TEXT("A point 1,000 under the view is 1,000 deeper"), Depth, 6000.0f);
		UWorldWeatherMath::OcclusionCoords(View + FVector(0.0, -10000.0, 0.0), Origin, Forward, U, V, UV, Depth);
		TestTrue(TEXT("The south edge is V 1 (down the texture)"), UV.Equals(FVector2D(0.5, 1.0), 1.0e-6));
		UWorldWeatherMath::OcclusionCoords(View + FVector(-5000.0, 5000.0, 0.0), Origin, Forward, U, V, UV, Depth);
		TestTrue(TEXT("North-west is the upper right quarter"), UV.Equals(FVector2D(0.75, 0.25), 1.0e-6));
	}
	// A planet: a radial up; the frame follows it, the point under the view is still the centre at the height.
	{
		const FVector Up = FVector(0.3, -0.5, 0.8).GetSafeNormal();
		const FVector View = Up * 250000.0;
		FVector Origin, Forward, U, V, CameraUp;
		UWorldWeatherMath::OcclusionFrame(View, Up, 4000.0f, 8000.0f, Origin, Forward, U, V, CameraUp);
		TestTrue(TEXT("Forward is the planet's down"), Forward.Equals(-Up, 1.0e-6));
		TestTrue(TEXT("The camera's up is tangent"), FMath::IsNearlyZero(FVector::DotProduct(CameraUp, Up), 1.0e-6) && CameraUp.Z > 0.0);
		TestTrue(TEXT("U and V are tangent"), FMath::IsNearlyZero(FVector::DotProduct(U, Up), 1.0e-9) && FMath::IsNearlyZero(FVector::DotProduct(V, Up), 1.0e-9));
		TestTrue(TEXT("U and V are orthogonal"), FMath::IsNearlyZero(FVector::DotProduct(U, V), 1.0e-12));
		TestTrue(TEXT("U and V span the width"), FMath::IsNearlyEqual(U.Size() * 16000.0, 1.0, 1.0e-6) && FMath::IsNearlyEqual(V.Size() * 16000.0, 1.0, 1.0e-6));
		FVector2D UV;
		float Depth = 0.0f;
		UWorldWeatherMath::OcclusionCoords(View - Up * 700.0, Origin, Forward, U, V, UV, Depth);
		TestTrue(TEXT("Under the view: the centre"), UV.Equals(FVector2D(0.5, 0.5), 1.0e-6));
		TestEqual(TEXT("... 700 deeper than the height"), Depth, 4700.0f, 1.0e-2f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The viewer's precipitation component in a game world (P2): the capture is orthographic scene depth that never captures
// by itself; with no systems named it stays inactive at any rate; the settings' systems are what it draws.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherPrecipitationSetupTest, "WorldWeather.Viewer.Precipitation", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherPrecipitationSetupTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldWeatherPrecipitationTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AWorldWeatherViewer* Viewer = World->SpawnActor<AWorldWeatherViewer>();
	// A bare test world has no game mode to dispatch BeginPlay to the actors: the component loads the systems there.
	World->GetWorldSettings()->NotifyBeginPlay();
	if (TestNotNull(TEXT("A viewer spawns"), Viewer) && TestNotNull(TEXT("... with a precipitation component"), Viewer->Precipitation.Get()))
	{
		UWorldWeatherPrecipitationComponent* P = Viewer->Precipitation;
		TestNotNull(TEXT("The particles"), P->Particles.Get());
		if (TestNotNull(TEXT("The capture"), P->Occlusion.Get()))
		{
			TestTrue(TEXT("Orthographic"), P->Occlusion->ProjectionType == ECameraProjectionMode::Orthographic);
			TestTrue(TEXT("Scene depth"), P->Occlusion->CaptureSource == SCS_SceneDepth);
			TestFalse(TEXT("Never every frame"), P->Occlusion->bCaptureEveryFrame);
			TestFalse(TEXT("Never on movement"), P->Occlusion->bCaptureOnMovement);
			TestFalse(TEXT("No lighting"), P->Occlusion->ShowFlags.Lighting);
			TestFalse(TEXT("No particles (rain must not occlude rain)"), P->Occlusion->ShowFlags.Particles);
			TestFalse(TEXT("No translucency"), P->Occlusion->ShowFlags.Translucency);
		}
		TestTrue(TEXT("Inactive at start"), P->GetActiveType() == EWorldWeatherPrecipitation::None);
		// A rainy condition with no system named in the settings (the test environment): still inactive, no capture, rate 0.
		FWorldWeatherConditions C;
		C.PrecipitationType = EWorldWeatherPrecipitation::Rain;
		C.PrecipitationRate = 0.8f;
		C.Exposure = 1.0f;
		C.Up = FVector::UpVector;
		P->UpdateWeather(C, FVector(100.0, 200.0, 300.0), 0.016f);
		const bool bHasRain = !UWorldWeatherSettings::Get()->RainSystem.IsNull() && UWorldWeatherSettings::Get()->RainSystem.IsValid();
		if (!bHasRain)
		{
			TestTrue(TEXT("No rain system: inactive"), P->GetActiveType() == EWorldWeatherPrecipitation::None);
			TestEqual(TEXT("No rain system: rate 0"), P->GetActiveRate(), 0.0f);
			TestFalse(TEXT("No rain system: no capture"), P->IsOcclusionCapturing());
		}
		else
		{
			AddInfo(TEXT("A rain system is configured: the component activates it"));
			TestTrue(TEXT("Rain active"), P->GetActiveType() == EWorldWeatherPrecipitation::Rain);
		}
		C.PrecipitationType = EWorldWeatherPrecipitation::None;
		C.PrecipitationRate = 0.0f;
		P->UpdateWeather(C, FVector(100.0, 200.0, 300.0), 0.016f);
		TestTrue(TEXT("Nothing falling: inactive"), P->GetActiveType() == EWorldWeatherPrecipitation::None);
		TestEqual(TEXT("Nothing falling: rate 0"), P->GetActiveRate(), 0.0f);
	}
	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The viewer's player-facing components (P4): the ambience, the lens and the lightning exist; without sounds or a lens
// material they stay silent and weightless; a strike reaches the lightning component and queues thunder with the sound's
// delay; the wind push's inputs are the conditions'.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherPlayerComponentsTest, "WorldWeather.Viewer.Player", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherPlayerComponentsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldWeatherPlayerTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	UWorldWeatherSubsystem* Weather = World->GetSubsystem<UWorldWeatherSubsystem>();
	AWorldWeatherViewer* Viewer = World->SpawnActor<AWorldWeatherViewer>();
	// A bare test world has no game mode, and the game mode is what dispatches BeginPlay to the actors: the lightning
	// component binds the subsystem's delegate there.
	World->GetWorldSettings()->NotifyBeginPlay();
	if (TestNotNull(TEXT("The weather"), Weather) && TestNotNull(TEXT("A viewer spawns"), Viewer)
		&& TestNotNull(TEXT("... with ambience"), Viewer->Ambience.Get()) && TestNotNull(TEXT("... a lens"), Viewer->Lens.Get()) && TestNotNull(TEXT("... lightning"), Viewer->Lightning.Get()))
	{
		FWorldWeatherConditions C;
		C.PrecipitationType = EWorldWeatherPrecipitation::Rain;
		C.PrecipitationRate = 0.8f;
		C.Exposure = 1.0f;
		C.Up = FVector::UpVector;
		C.WindVector = FVector(600.0, 0.0, 0.0);
		Viewer->Ambience->UpdateWeather(C, 1.0f, 0.016f);
		const bool bHasRain = Viewer->Ambience->RainLight && Viewer->Ambience->RainLight->Sound != nullptr;
		if (!bHasRain)
		{
			TestTrue(TEXT("No rain sound named: the loop's volume is 0"), Viewer->Ambience->GetVolumes().X == 0.0f);
		}
		TestEqual(TEXT("Open: no low-pass"), Viewer->Ambience->GetLowPassHz(), 20000.0f, 1.0f);
		C.Exposure = 0.0f;
		Viewer->Ambience->UpdateWeather(C, 1.0f, 0.016f);
		TestEqual(TEXT("Sheltered: the settings' interior cutoff"), Viewer->Ambience->GetLowPassHz(), UWorldWeatherSettings::Get()->InteriorLowPassHz, 1.0f);

		// The lens without a material: weightless whatever falls.
		Viewer->Lens->UpdateWeather(C, 1.0f, 1.0f, 1.0f);
		const bool bHasLens = !UWorldWeatherSettings::Get()->LensMaterial.IsNull();
		if (!bHasLens)
		{
			TestEqual(TEXT("No lens material: weight 0"), Viewer->Lens->GetWeight(), 0.0f);
		}

		// A strike 3,430 uu away: one strike counted, its thunder queued (no sound: it still queues, plays nothing).
		const FVector Here = Viewer->GetActorLocation();
		Weather->Strike(Here + FVector(3430.0, 0.0, 0.0));
		TestEqual(TEXT("The lightning component saw the strike"), Viewer->Lightning->GetStrikeCount(), 1);
		TestEqual(TEXT("The thunder is queued"), Viewer->Ambience->GetPendingThunder(), 1);
		TestEqual(TEXT("Not played yet (0.1 s away)"), Viewer->Ambience->GetThunderPlayed(), 0);
		Weather->Strike(Here + FVector(UWorldWeatherSettings::Get()->ThunderMaxDistance + 1000.0f, 0.0, 0.0));
		TestEqual(TEXT("A strike past the thunder's reach queues nothing"), Viewer->Ambience->GetPendingThunder(), 1);
	}
	World->EndPlay(EEndPlayReason::Quit);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The bolt (the foliage and bolt plan's L1): the strike light's fade and the bolt's frame, pure.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherLightningBoltTest, "WorldWeather.Lightning.Bolt", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherLightningBoltTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full at the strike"), UWorldWeatherLightningComponent::FlashIntensity(0.f, 0.5f), 1.f, 1.0e-6f);
	TestEqual(TEXT("A quarter at half the time"), UWorldWeatherLightningComponent::FlashIntensity(0.25f, 0.5f), 0.25f, 1.0e-6f);
	TestEqual(TEXT("Out at the end"), UWorldWeatherLightningComponent::FlashIntensity(0.5f, 0.5f), 0.f, 1.0e-6f);
	TestEqual(TEXT("Out after"), UWorldWeatherLightningComponent::FlashIntensity(2.f, 0.5f), 0.f, 1.0e-6f);
	TestEqual(TEXT("A zero duration does not divide"), UWorldWeatherLightningComponent::FlashIntensity(0.1f, 0.f), 0.f, 1.0e-6f);
	const FVector Up = FVector(0.3, -0.4, 0.866).GetSafeNormal();
	const FRotator R = UWorldWeatherLightningComponent::BoltRotation(Up);
	TestTrue(TEXT("The bolt's +Z is the local up"), FRotationMatrix(R).GetUnitAxis(EAxis::Z).Equals(Up, 1.0e-4));
	TestTrue(TEXT("A zero up stands on world Z"), FRotationMatrix(UWorldWeatherLightningComponent::BoltRotation(FVector::ZeroVector)).GetUnitAxis(EAxis::Z).Equals(FVector::UpVector, 1.0e-4));
	return true;
}

// ---------------------------------------------------------------------------
// The mirror table's instance targets (the foliage and bolt plan's D67): an entry naming an instance group writes its
// source x scale + offset on every instance registered under the group each tick; registering twice is one; an
// unregistered instance keeps its last value and takes no more; an instance gone is dropped.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldWeatherMirrorInstancesTest, "WorldWeather.Subsystem.MirrorInstances", WORLDWEATHER_TEST_FLAGS)
bool FWorldWeatherMirrorInstancesTest::RunTest(const FString& Parameters)
{
	static const FName Group(TEXT("TestFoliageWind"));
	static const FName Param(TEXT("Wind Intensity"));
	const auto ScalarOf = [](const UMaterialInstanceDynamic* Instance, float& OutValue)
	{
		for (const FScalarParameterValue& P : Instance->ScalarParameterValues)
		{
			if (P.ParameterInfo.Name == Param)
			{
				OutValue = P.ParameterValue;
				return true;
			}
		}
		return false;
	};

	// The entry goes on the settings for the world's Initialize (where the table is resolved), then comes off again.
	UWorldWeatherSettings* Settings = GetMutableDefault<UWorldWeatherSettings>();
	const TArray<FWorldWeatherMirrorEntry> SavedTable = Settings->MirrorTable;
	FWorldWeatherMirrorEntry& Entry = Settings->MirrorTable.AddDefaulted_GetRef();
	Entry.InstanceGroup = Group;
	Entry.Parameter = Param;
	Entry.Source = EWorldWeatherMirrorSource::Precipitation;
	Entry.Scale = 2.0f;
	Entry.Offset = 0.25f;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WorldWeatherMirrorInstancesTest"));
	Settings->MirrorTable = SavedTable;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UWorldWeatherSubsystem* Weather = World->GetSubsystem<UWorldWeatherSubsystem>();
	if (TestNotNull(TEXT("The game world has weather"), Weather))
	{
		UMaterialInterface* Parent = UMaterial::GetDefaultMaterial(MD_Surface);
		UMaterialInstanceDynamic* A = UMaterialInstanceDynamic::Create(Parent, World);
		UMaterialInstanceDynamic* B = UMaterialInstanceDynamic::Create(Parent, World);
		UMaterialInstanceDynamic* Other = UMaterialInstanceDynamic::Create(Parent, World);
		Weather->RegisterMirrorInstance(Group, A);
		Weather->RegisterMirrorInstance(Group, A);
		Weather->RegisterMirrorInstance(Group, B);
		Weather->RegisterMirrorInstance(TEXT("NoEntryNamesThis"), Other);
		TestEqual(TEXT("Two instances in the group (A twice is one)"), Weather->GetMirrorInstanceCount(Group), 2);
		TestTrue(TEXT("ww.Weather names the group"), Weather->DescribeMirrorInstances().Contains(TEXT("TestFoliageWind: 2")));

		Weather->SetWeather(WorldWeatherTags::Weather_Preset_Storm, 0.0f);
		Weather->Tick(0.1f);
		const float Expected = Weather->GetCurrentFront().Precipitation * 2.0f + 0.25f;
		float ValueA = -1.0f, ValueB = -1.0f, ValueOther = -1.0f;
		TestTrue(TEXT("A takes the entry's parameter"), ScalarOf(A, ValueA));
		TestEqual(TEXT("... the source x scale + offset"), ValueA, Expected, 1.0e-5f);
		TestTrue(TEXT("B too"), ScalarOf(B, ValueB) && FMath::IsNearlyEqual(ValueB, Expected, 1.0e-5f));
		TestFalse(TEXT("A group no entry names is not written"), ScalarOf(Other, ValueOther));

		// Unregistered: A keeps its value and takes no more; B follows the next front.
		TestTrue(TEXT("Unregister A"), Weather->UnregisterMirrorInstance(Group, A));
		TestFalse(TEXT("Unregister A again: not there"), Weather->UnregisterMirrorInstance(Group, A));
		Weather->SetWeather(WorldWeatherTags::Weather_Preset_Clear, 0.0f);
		Weather->Tick(0.1f);
		const float ExpectedClear = Weather->GetCurrentFront().Precipitation * 2.0f + 0.25f;
		ScalarOf(A, ValueA);
		ScalarOf(B, ValueB);
		TestEqual(TEXT("A keeps the storm's value"), ValueA, Expected, 1.0e-5f);
		TestEqual(TEXT("B takes the clear front's"), ValueB, ExpectedClear, 1.0e-5f);
		TestEqual(TEXT("One instance left"), Weather->GetMirrorInstanceCount(Group), 1);

		// Gone: B is dropped (held weakly) and the next tick writes nothing.
		B->MarkAsGarbage();
		TestEqual(TEXT("B gone: none counted"), Weather->GetMirrorInstanceCount(Group), 0);
		Weather->Tick(0.1f);
		TestTrue(TEXT("The empty group reads empty"), Weather->DescribeMirrorInstances().Contains(TEXT("TestFoliageWind: 0")));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#undef WORLDWEATHER_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
