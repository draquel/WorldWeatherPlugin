# WorldWeatherPlugin

Weather for Unreal Engine 5.8 worlds, shaped like the World Clock plugin it depends on: the authority owns a timeline of
weather fronts, publishes one sync point, and every machine derives the present from the synchronised clock. Conditions
(precipitation type, wind, fog, temperature, wetness, snow cover, exposure) are resolved per position through provider
interfaces a game sets, so the plugin stays engine-only.

The reference: `.claude/instructions.md` and `Documentation/WEATHER_SYSTEM.md`. The design record is the parent
project's `Documentation/WEATHER_SYSTEM_PLAN.md`.
