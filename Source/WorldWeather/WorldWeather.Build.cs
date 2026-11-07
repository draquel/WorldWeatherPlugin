// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

public class WorldWeather : ModuleRules
{
	public WorldWeather(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// Weather is identified by gameplay tags (Weather.*) so consumers match without linking us.
				"GameplayTags",
				// Settings in Project Settings / DefaultGame.ini.
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// The timeline runs on the world clock's synchronised seconds (the plan's D59).
				"WorldClock",
				"NetCore",
				// The viewer's precipitation: a Niagara system per type, its user parameters set from the conditions (P2).
				"Niagara",
				// The GPU frame time ww.Weather prints (RHIGetGPUFrameCycles): the viewer effects' A/B.
				"RHI",
			}
		);
	}
}
