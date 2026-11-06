// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/** Native gameplay tags for the weather. Consumers in other plugins match these, never a preset index. */
namespace WorldWeatherTags
{
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather);

	// The presets the default table has (a table may add its own under Weather.Preset).
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Clear);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Overcast);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Rain);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Storm);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Snow);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Fog);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Preset_Windy);

	// What falls at the place.
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Precipitation);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Precipitation_None);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Precipitation_Rain);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Precipitation_Snow);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Precipitation_Sleet);

	// How hard (the rate at the place against the settings' threshold).
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Intensity);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Intensity_Light);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Intensity_Heavy);

	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Storm);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Fog);

	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Wind);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Wind_Calm);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Wind_Breeze);
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Wind_Strong);

	/** The place is under cover (the exposure under the settings' threshold). */
	WORLDWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_Sheltered);
}
