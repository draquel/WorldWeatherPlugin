// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWorldWeatherEditor, Log, All);

/** Editor-only: the content library the weather's content script calls (the precipitation systems built from a pack's). */
class FWorldWeatherEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
