// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeather.h"

#define LOCTEXT_NAMESPACE "FWorldWeatherModule"

DEFINE_LOG_CATEGORY(LogWorldWeather);

void FWorldWeatherModule::StartupModule()
{
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather module started"));
}

void FWorldWeatherModule::ShutdownModule()
{
	UE_LOG(LogWorldWeather, Log, TEXT("WorldWeather module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWorldWeatherModule, WorldWeather)
