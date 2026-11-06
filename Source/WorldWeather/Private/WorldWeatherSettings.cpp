// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherSettings.h"

UWorldWeatherSettings::UWorldWeatherSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("World Weather");
}

FWorldWeatherRules UWorldWeatherSettings::MakeRules() const
{
	FWorldWeatherRules Rules;
	Rules.SnowBelowC = SnowBelowC;
	Rules.SleetBandC = SleetBandC;
	Rules.DryPrecipitationScale = DryPrecipitationScale;
	Rules.HeavyPrecipitationAbove = HeavyPrecipitationAbove;
	Rules.FogTagAbove = FogTagAbove;
	Rules.CalmWindBelow = CalmWindBelow;
	Rules.StrongWindAbove = StrongWindAbove;
	Rules.ShelteredBelow = ShelteredBelow;
	return Rules;
}

FWorldWeatherAccumulatorRates UWorldWeatherSettings::MakeRates() const
{
	FWorldWeatherAccumulatorRates Rates;
	Rates.WetnessRiseSeconds = FMath::Max(WetnessRiseSeconds, 1.0f);
	Rates.DryingSeconds = FMath::Max(DryingSeconds, 1.0f);
	Rates.SnowRiseSeconds = FMath::Max(SnowRiseSeconds, 1.0f);
	Rates.MeltSeconds = FMath::Max(MeltSeconds, 1.0f);
	return Rates;
}

const UWorldWeatherSettings* UWorldWeatherSettings::Get()
{
	return GetDefault<UWorldWeatherSettings>();
}
