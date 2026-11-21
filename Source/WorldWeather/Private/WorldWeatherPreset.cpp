// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherPreset.h"
#include "WorldWeatherTags.h"

namespace
{
	/** A weight at a band value: the three weights at 0, 0.5 and 1, linear between. */
	float BandWeight(float Value, float AtZero, float AtHalf, float AtOne)
	{
		const float V = FMath::Clamp(Value, 0.0f, 1.0f);
		return V < 0.5f ? FMath::Lerp(AtZero, AtHalf, V * 2.0f) : FMath::Lerp(AtHalf, AtOne, (V - 0.5f) * 2.0f);
	}
}

float FWorldWeatherPreset::WeightAt(float Temperature, float Moisture) const
{
	return BandWeight(Temperature, ColdWeight, TemperateWeight, HotWeight) * BandWeight(Moisture, DryWeight, ModerateWeight, WetWeight);
}

const FWorldWeatherPreset* UWorldWeatherPresetTable::FindByTag(const FGameplayTag& Tag) const
{
	const int32 Index = IndexOfTag(Tag);
	return Index >= 0 ? &Presets[Index] : nullptr;
}

int32 UWorldWeatherPresetTable::IndexOfTag(const FGameplayTag& Tag) const
{
	return Presets.IndexOfByPredicate([&Tag](const FWorldWeatherPreset& P) { return P.Tag == Tag; });
}

void UWorldWeatherPresetTable::ResetToDefaults()
{
	FillDefaults(Presets);
}

void UWorldWeatherPresetTable::FillDefaults(TArray<FWorldWeatherPreset>& Out)
{
	Out.Reset(7);
	auto Add = [&Out](const FGameplayTag& Tag, float Cover, float Density, float Precip, float Fog, float Wind, float Gust, float Thunder, float TempOffset, float Humidity,
		float HoldMin, float HoldMax, float Blend, float Cold, float Temperate, float Hot, float Dry, float Moderate, float Wet)
	{
		FWorldWeatherPreset& P = Out.AddDefaulted_GetRef();
		P.Tag = Tag;
		P.Front.CloudCover = Cover;
		P.Front.CloudDensity = Density;
		P.Front.Precipitation = Precip;
		P.Front.FogDensity = Fog;
		P.Front.WindSpeed = Wind;
		P.Front.WindAzimuth = 0.0f;
		P.Front.Gustiness = Gust;
		P.Front.ThunderRate = Thunder;
		P.Front.TemperatureOffset = TempOffset;
		P.Front.Humidity = Humidity;
		P.HoldMinutesMin = HoldMin;
		P.HoldMinutesMax = HoldMax;
		P.BlendMinutes = Blend;
		P.ColdWeight = Cold;
		P.TemperateWeight = Temperate;
		P.HotWeight = Hot;
		P.DryWeight = Dry;
		P.ModerateWeight = Moderate;
		P.WetWeight = Wet;
	};
	//                                       cover dens  prec  fog   wind  gust  thun  temp  humid  hold      blend  cold temp  hot   dry   mod   wet
	Add(WorldWeatherTags::Weather_Preset_Clear,    0.10f, 0.30f, 0.0f, 0.00f, 150.f, 0.2f, 0.0f,  2.0f, 0.20f,  60.f, 180.f, 10.f,  1.0f, 1.0f, 1.2f, 1.5f, 1.0f, 0.7f);
	Add(WorldWeatherTags::Weather_Preset_Overcast, 0.80f, 0.60f, 0.0f, 0.10f, 250.f, 0.3f, 0.0f, -1.0f, 0.50f,  40.f, 120.f, 15.f,  1.0f, 1.0f, 0.8f, 0.6f, 1.0f, 1.2f);
	Add(WorldWeatherTags::Weather_Preset_Rain,     0.90f, 0.80f, 0.6f, 0.20f, 350.f, 0.4f, 0.0f, -3.0f, 0.90f,  30.f,  90.f, 10.f,  0.5f, 1.0f, 1.0f, 0.1f, 0.8f, 1.5f);
	Add(WorldWeatherTags::Weather_Preset_Storm,    1.00f, 1.00f, 1.0f, 0.30f, 900.f, 0.8f, 0.5f, -4.0f, 1.00f,  15.f,  40.f,  8.f,  0.3f, 0.8f, 1.2f, 0.05f, 0.4f, 1.0f);
	Add(WorldWeatherTags::Weather_Preset_Snow,     0.90f, 0.70f, 0.5f, 0.25f, 300.f, 0.3f, 0.0f, -8.0f, 0.60f,  40.f, 120.f, 12.f,  2.0f, 0.3f, 0.0f, 0.3f, 0.8f, 1.2f);
	Add(WorldWeatherTags::Weather_Preset_Fog,      0.50f, 0.40f, 0.0f, 0.90f,  60.f, 0.1f, 0.0f, -1.0f, 0.80f,  20.f,  60.f, 12.f,  1.0f, 1.0f, 0.6f, 0.2f, 0.8f, 1.3f);
	Add(WorldWeatherTags::Weather_Preset_Windy,    0.40f, 0.40f, 0.0f, 0.00f, 800.f, 0.7f, 0.0f, -1.0f, 0.30f,  20.f,  60.f, 10.f,  1.0f, 1.0f, 1.0f, 1.3f, 1.0f, 0.8f);
}
