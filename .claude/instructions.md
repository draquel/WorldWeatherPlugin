# WorldWeatherPlugin — Claude instructions

> State as of 2026-11-06 (P0 of the weather system plan: the plugin and the core). This file is the plugin's reference
> with `Documentation/WEATHER_SYSTEM.md`; its design record is the parent project's `Documentation/WEATHER_SYSTEM_PLAN.md`
> (D58-D66, phases P0-P6).

## Plugin identity
- One runtime module `WorldWeather`. Log category `LogWorldWeather`. Depends on Core / CoreUObject / Engine /
  GameplayTags / DeveloperSettings (+ WorldClock, NetCore privately). **No game-framework or voxel plugin may be a
  dependency here**; what is specific to a world comes in through the provider interfaces, set by a game-side bridge.
- Tags: `Weather.Preset.{Clear,Overcast,Rain,Storm,Snow,Fog,Windy}`, `Weather.Precipitation.{None,Rain,Snow,Sleet}`,
  `Weather.Intensity.{Light,Heavy}`, `Weather.Storm`, `Weather.Fog`, `Weather.Wind.{Calm,Breeze,Strong}`,
  `Weather.Sheltered` (native, `WorldWeatherTags.h`). Consumers match tags, never a preset index.

## Architecture (the plan's principles 1, 2 and 5)
- **Weather is world state with the clock's shape.** `UWorldWeatherSubsystem` (tickable world subsystem, Game / PIE
  worlds; initialised after `UWorldClockSubsystem`): the authority owns a timeline of *fronts*. When no front exists it
  draws the first (`StartPreset` or the schedule); when a front's blend and hold have run out it draws the next
  (`UWorldWeatherMath::PickNext`: the schedule seed, the clock day and a draw index seed a stream, the presets' affinities
  at the climate of the reference point weight it, never the same preset twice in a row). Every draw and setter publishes
  one `FWorldWeatherSyncPoint` through `AWorldWeatherState` (an always-relevant info actor, net update 2, no OnRep, no
  RPC): the front left and the front arrived at, their tags, the start in clock seconds, the blend and hold lengths, the
  accumulators at the start, a serial.
- **Every machine derives the present** from that sync point and `GetClockSeconds()` (the clock's day and seconds, so a
  client's extrapolated clock gives the same weather and a paused clock pauses it): `GetCurrentFront` (the member lerp,
  the azimuth the short way), `GetCurrentPreset` (the front left until the blend is half way, then the one arrived at),
  `GetWetness` / `GetSnowCover` (the accumulators integrated forward from the sync point in game-minute steps, at most
  256: the same inputs give the same values everywhere, so nothing is republished for them). `OnWeatherChanged(Old, New)`
  fires on the tag's edge and `OnLightning(Location)` on a scheduled strike, both primed on the first tick as the clock's
  edges are; strike *times* are a hash of the clock minute against the front's `ThunderRate`, strike *places* are this
  machine's, around its first local player's view.
- **A query resolves the global front at a place.** `GetConditionsAt(Pos, Requester)`: the climate provider's sample
  (temperature and moisture 0..1, altitude, up, underwater) or the settings' defaults (a Z-up plane), degrees from the
  settings' linear map or curve less the night's cooling by the clock's local daylight plus the front's offset
  (`GetTemperatureAt`), the precipitation type by those degrees (snow at or under `SnowBelowC`, sleet within
  `SleetBandC`, rain above; none under water), the rate by the moisture, the wind tangent to the up in the local frame
  with the gust phase, the accumulators read as snow where it is cold and wet where it is not, the exposure
  (`GetExposureAt`: the shelter provider's answer, else a trace cone of `ShelterTraces` rays over `ShelterDistance`,
  cached `ShelterInterval` per requester), and the tags.
- **The local frame** (`UWorldWeatherMath::LocalFrame`) is cyclic with the world's axes: north is +Z projected onto the
  tangent plane (toward a planet's pole; +Y on a plane), east is north x up (the side of increasing longitude on a
  planet, where the clock's sun rises; +X on a plane). A front's `WindAzimuth` is degrees from that north toward that
  east.
- `UWorldWeatherPresetTable` (a data asset the settings name; `FillDefaults` builds the seven defaults in memory when
  none is named, `ResetToDefaults` for a content script to bake the asset): rows of `FWorldWeatherPreset` (tag, front,
  hold range and blend in game minutes, weights by temperature band and moisture band; `WeightAt` blends the three
  weights linearly over 0, 0.5, 1).
- `UWorldWeatherSettings` (`[/Script/WorldWeather.WorldWeatherSettings]`): the table, `ScheduleSeed`, `bAutoSchedule`,
  `StartPreset`; the temperature map (`ColdTemperatureC`, `HotTemperatureC`, `TemperatureCurve`, `NightCoolingC`,
  `DefaultTemperature`, `DefaultMoisture`); the precipitation thresholds and the accumulator rates; the shelter trace;
  the tag thresholds; `LightningRadius`.
- `IWorldWeatherClimateProvider` (`GetClimateAt`, `GetWeatherSeed`) and `IWorldWeatherShelterProvider`
  (`GetExposureAt`): native interfaces a bridge implements and sets with `SetClimateProvider` / `SetShelterProvider`
  (VoxelWorldPOI's `UPOIWeatherBridge`, P2). Without them every place is the default plane.
- `FWorldWeatherSaveState` + `ExportState` / `ImportState` for a save system (the sync point, the schedule's index, the
  lock); an import re-primes the edges so a loaded storm fires no change.
- Setters (`SetWeather(Tag, BlendSeconds)`, `SetWind`, `SkipToNext`, `SetLocked`, `ImportState`) are authority-only
  and warn elsewhere; `Strike(Location)` is local to the machine that calls it.

## Rules
- Never read the authority's `Sync` on a client: every reader goes through `FindSync()` / the public getters, which
  resolve the replicated state's point.
- Effects are consumers: nothing in this plugin moves a light, a material or a particle in P0; the sky goes through the
  clock controller's modifier slot (P1), the materials through VoxelWorlds' collections (P3), the viewer actor's
  components (P2, P4). Keep this plugin free of gameplay: NPC shelter, wet grip and cold belong to consumers that bind
  the delegates or read `GetConditionsAt`.
- A change to the schedule, the blend, the accumulators or the resolution changes what every machine computes: keep the
  functions pure in `UWorldWeatherMath` and the tests on them.
- Tests: `WorldWeather.*` (EditorContext | EngineFilter): `Math.Blend`, `Math.Schedule`, `Math.PrecipitationType`,
  `Math.WindVector`, `Math.Accumulators`, `Math.Lightning`, `Sync.Resolve` (pure); `Subsystem.Defaults` (a game world
  without a provider: the defaults, SetWeather, the blend, the schedule, the lock, the export round trip, the wind).

## Console
- `ww.Weather` (the present preset and blend, the front, the accumulators, the next draw, the conditions where the pawn
  stands), `ww.Set <Preset|Weather.Preset.X> [BlendSeconds]` (authority; no argument lists the presets), `ww.Lock 0|1`,
  `ww.Wind <Azimuth> <Speed>`, `ww.Strike [Distance]` (this machine), `ww.Next`. Registered in `WorldWeatherConsole.cpp`;
  the game module's `vox.Weather` and `vox.Wind` run `ww.Weather` and `ww.Wind`. All of them:
  `Documentation/EDITOR_CONSOLE_GUIDE.md` §7 (project root).
