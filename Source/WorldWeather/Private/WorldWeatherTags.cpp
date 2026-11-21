// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherTags.h"

namespace WorldWeatherTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather, "Weather", "Root of the weather tags");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset,          "Weather.Preset",          "Root of the weather presets (one per preset table row)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Clear,    "Weather.Preset.Clear",    "A clear sky, a light breeze");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Overcast, "Weather.Preset.Overcast", "A grey ceiling, no precipitation");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Rain,     "Weather.Preset.Rain",     "Steady precipitation (rain or snow by the temperature)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Storm,    "Weather.Preset.Storm",    "A downpour, strong gusting wind, lightning");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Snow,     "Weather.Preset.Snow",     "A cold front with precipitation: snow where it is cold");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Fog,      "Weather.Preset.Fog",      "Dense fog, still air");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Preset_Windy,    "Weather.Preset.Windy",    "A strong gusting wind under a broken sky");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Precipitation,       "Weather.Precipitation",       "Root of what falls at the place");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Precipitation_None,  "Weather.Precipitation.None",  "Nothing falls here");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Precipitation_Rain,  "Weather.Precipitation.Rain",  "Rain falls here");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Precipitation_Snow,  "Weather.Precipitation.Snow",  "Snow falls here");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Precipitation_Sleet, "Weather.Precipitation.Sleet", "Sleet falls here (just above freezing)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Intensity,       "Weather.Intensity",       "Root of the precipitation intensity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Intensity_Light, "Weather.Intensity.Light", "Light precipitation");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Intensity_Heavy, "Weather.Intensity.Heavy", "Heavy precipitation");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Storm, "Weather.Storm", "A storm: lightning is scheduled");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Fog,   "Weather.Fog",   "Weather fog thick enough to notice");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Wind,        "Weather.Wind",        "Root of the wind bands");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Wind_Calm,   "Weather.Wind.Calm",   "Still air");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Wind_Breeze, "Weather.Wind.Breeze", "A breeze");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Wind_Strong, "Weather.Wind.Strong", "A strong wind");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather_Sheltered, "Weather.Sheltered", "The place is under cover");
}
