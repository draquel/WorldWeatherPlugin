# WorldWeatherPlugin — Claude instructions

> State as of 2026-11-06 (P0, the plugin and the core, and P1, the sky, of the weather system plan). This file is the plugin's reference
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

## The sky (P1; the plan's D61, D62, D64)
- **The clock controller's slot.** `TickSky`, every tick on every machine: the present front fills an `FDayNightModifiers`
  (`FillModifiers`, pure, from the settings' Sky section: the sun scaled by `1 - cover x SunDimByCloudCover -
  precipitation x SunDimByPrecipitation` down to `SunScaleMin`, the sky light by `1 - cover x SkyLightDimByCloudCover`,
  the height fog's density by `1 + fog x FogDensityScaleByFog`, its colour toward `FogTintOvercast` by the cover, the
  clouds' coverage and density parameters between their Min and Max by the front's cover and density, the lightning
  flash) and hands it to the first `ADayNightController` (found by class, looked for again every second until one
  exists). The controller applies it; this plugin writes no light, fog or cloud itself. `bDriveSky` turns it off.
- **The engine's cloud parameters are not 0..1.** `m_SimpleVolumetricCloud_Inst` authors `Cloud_GlobalCoverage` -0.2
  (an offset on its weather map) and `Cloud_GlobalDensity` 0.008: the defaults map cover 0..1 to -0.35..0.3 and density
  0..1 to 0.006..0.12 (seen on `VoxelDemo`: a few cumulus, a broken grey overcast, a closed ceiling under a storm).
- **`MPC_Weather`** (`WeatherCollection`; `Scripts/configure_weather.py` makes `/WorldWeatherPlugin/MPC_Weather`):
  the scalars CloudCover, CloudDensity, Precipitation, FogDensity, WindSpeed, WindIntensity (over `StrongWindAbove`),
  GustFactor, Gustiness, ThunderRate, Humidity, Wetness, SnowCover, TemperatureC, LightningFlash and the vector
  WindDirection (xyz the unit wind at this machine's view, w the speed), written every tick. **The mirror table**
  (`MirrorTable`: collection, parameter, an `EWorldWeatherMirrorSource`, scale, offset) writes the same sources into any
  other collection (VoxelWorlds' wind inputs in P3, a pack's own wind collection) with no edit to its owner.
- **The viewer** (`AWorldWeatherViewer`, transient, not replicated): one per local player controller
  (`bAutoSpawnViewers`; none on a dedicated server), following the player's camera in `TG_PostUpdateWork`, carrying a
  `ULocalFogVolumeComponent` of radius `LocalFogRadius` whose radial extinction is the front's fog times
  `LocalFogExtinctionMax` (meant as the weather's fog everywhere, on a planet too where the height fog is hidden); the cvar
  `ww.Viewer.Fog 0` is the A/B. **It renders nothing yet**: local fog volumes draw nothing in this project, the engine's own
  actor included (G134); the component stays so a fix lights it. Later phases add precipitation, ambience, the lens and lightning to it.
- **The flash.** `Strike` (the scheduled minute's or `ww.Strike`) sets `FlashRemaining` to `LightningFlashSeconds`;
  `GetLightningFlash` fades it to 0, into the slot and the collection.
- **The cost.** The slot, the collection and the mirror writes: 0.02 ms a tick on `VoxelDemo` (`ww.Weather` prints the
  last tick's and a one-second average: `GetSkyWriteCost`).

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
- `ww.Weather` (the present preset and blend, the front, the sky controller and viewers and the sky-write cost, the
  accumulators, the next draw, the conditions where the pawn stands), `ww.Set <Preset|Weather.Preset.X> [BlendSeconds]` (authority; no argument lists the presets), `ww.Lock 0|1`,
  `ww.Wind <Azimuth> <Speed>`, `ww.Strike [Distance]` (this machine), `ww.Next`; the cvar `ww.Viewer.Fog 0|1`
  (`WorldWeatherViewer.cpp`). Registered in `WorldWeatherConsole.cpp`;
  the game module's `vox.Weather` and `vox.Wind` run `ww.Weather` and `ww.Wind`. All of them:
  `Documentation/EDITOR_CONSOLE_GUIDE.md` §7 (project root).
