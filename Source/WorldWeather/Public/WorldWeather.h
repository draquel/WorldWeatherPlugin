// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

WORLDWEATHER_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldWeather, Log, All);

class FWorldWeatherModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
