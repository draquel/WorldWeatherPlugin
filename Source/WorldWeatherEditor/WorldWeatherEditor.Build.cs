// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

public class WorldWeatherEditor : ModuleRules
{
	public WorldWeatherEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				// The content library edits Niagara systems' emitters and renderers (the precipitation systems built from the pack's).
				"Niagara",
				"NiagaraEditor",   // FNiagaraEditorUtilities::AddEmitterToSystem (the bolt)
				"WorldWeather",
			}
		);
	}
}
