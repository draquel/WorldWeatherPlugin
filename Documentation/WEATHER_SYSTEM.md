# The weather system

> The reference document of WorldWeatherPlugin, with `.claude/instructions.md`. The design record is the parent
> project's `Documentation/WEATHER_SYSTEM_PLAN.md` (the question, the survey, the architecture, D58-D66, P0-P6). Each
> phase adds its results paragraph here (section 6).

## 1. What it is

Weather as world state with the world clock's shape: the authority owns a timeline of fronts, publishes one sync point,
and every machine derives the present from the synchronised clock. A front is ten authored numbers (`FWorldWeatherFront`:
cloud cover and density, a precipitation rate, fog, wind speed and azimuth, gustiness, thunder rate, a temperature
offset, humidity); the world blends from one to the next over a preset's blend time and holds it for a drawn hold time.
What a front means at a place (`FWorldWeatherConditions`: rain, snow or sleet, the wind in the local frame, the
temperature, the ground's wetness or snow cover, the exposure, the tags) is resolved per position through two provider
interfaces a game sets, so the plugin stays engine-only.

## 2. The pieces

| Piece | Role |
|---|---|
| `UWorldWeatherSubsystem` | The timeline (the schedule, the sync point, the accumulators), `GetConditionsAt`, `GetExposureAt`, the delegates, the setters, the save state |
| `AWorldWeatherState` | The replication carrier: an always-relevant info actor with the sync point and the lock |
| `UWorldWeatherMath` | Pure: the blend, the schedule, the precipitation type, the local frame and the wind, the accumulators, the lightning schedule, the resolution |
| `UWorldWeatherPresetTable` | The fronts a world can have, with hold and blend times and climate affinities; seven defaults built in memory when no asset is named |
| `UWorldWeatherSettings` | The table, the seed, the temperature map, the thresholds, the rates, the shelter trace, the lightning radius |
| `IWorldWeatherClimateProvider`, `IWorldWeatherShelterProvider` | What is world-specific: the climate and frame at a place, the seed; shelter a trace cannot see |
| `WorldWeatherTags.h` | `Weather.*`: the presets, the precipitation type and intensity, storm, fog, the wind band, sheltered |
| `ww.*` | The console: `ww.Weather`, `ww.Set`, `ww.Lock`, `ww.Wind`, `ww.Strike`, `ww.Next` |

## 3. The timeline

The authority's tick: no front yet, draw the first (`StartPreset` when the settings name one, else the schedule's
draw with no blend); the blend and the hold run out, draw the next (`PickNext`). A draw seeds a stream with the
schedule seed (the settings' plus the climate provider's world seed), the clock day and the draw's index, weights the
presets by their affinities at the climate of the reference point (the first player's position, else the origin), and
never draws the present preset again when another has weight; the hold comes from the preset's range. `SetWeather`,
`SetWind`, `SkipToNext`, `SetLocked` and `ImportState` are the authority's other ways in; each publishes.

The sync point: the front left (the present resolved at the moment of the draw, so a blend interrupted blends on from
where it was), the front arrived at, their tags, the start in clock seconds (day 1 at 00:00 = 0), the blend and hold in
game seconds, the wetness and snow cover at the start, a serial. Every machine resolves the present from it and
`GetClockSeconds()`: alpha = clamp((now - start) / blend), the front = the member lerp (the azimuth the short way), the
preset = the one left until alpha reaches 0.5, then the one arrived at (the change event's edge), the accumulators
integrated forward in game-minute steps (at most 256, so a locked front stays cheap). Nothing is republished for drift:
the integration is a pure function of the sync point and the time, the same on every machine.

The accumulators: under precipitation the wetness rises toward 1 at the rate over `WetnessRiseSeconds` and the snow
cover over `SnowRiseSeconds`; without it the wetness decays over `DryingSeconds` (humid air up to half as fast) and the
snow over `MeltSeconds`. Both are global: at a place the snow cover counts where it is cold (the cold factor, 1 at or
under `SnowBelowC`, 0 at `SnowBelowC + SleetBandC`) and the wetness where it is not.

Lightning: a clock minute strikes when a hash of the seed and the minute falls under the front's `ThunderRate` (at most
one strike a minute), at a hashed second within it; every machine agrees on the time and picks its own place within
`LightningRadius` of its first local player's view. `ww.Strike` fires one by hand.

## 4. The query

`GetConditionsAt(Pos, Requester)`: the climate at the place (the provider's, else `DefaultTemperature` /
`DefaultMoisture` on a Z-up plane); degrees = the settings' linear map (or `TemperatureCurve`) of the climate's
temperature, less `NightCoolingC` times (1 - the clock's local daylight), plus the front's offset; the precipitation rate
= the front's times lerp(`DryPrecipitationScale`, 1, moisture), none under water; the type by the degrees; the wind =
speed times the gust factor along the azimuth in the local frame; the exposure = the shelter provider's answer, else a
trace cone (`ShelterTraces` rays along the up, the outer ones `ShelterConeDegrees` off it, over `ShelterDistance`, the
requester's actor ignored, cached `ShelterInterval` per requester); the tags.

The local frame is cyclic with the world's axes (east x north = up, as X x Y = Z): north is +Z projected onto the
tangent plane, toward a planet's pole, +Y on a plane; east is north x up, the side of increasing longitude on a planet
(where the clock's sun rises), +X on a plane (the clock's default sunrise bearing). A front's wind azimuth is degrees
from that north toward that east.

## 5. The sky (P1)

The clock controller stays the one writer of the sun, the sky light, the height fog and the clouds (D61): it gained a
modifier slot, `FDayNightModifiers`, identity by default, that the weather fills every tick on every machine from the
present front (`UWorldWeatherSubsystem::FillModifiers`, the settings' Sky section): the sun dimmed by the cloud cover
and the precipitation (to `SunScaleMin`), the sky light by the cover, the height fog's density scaled by the weather fog
and its colour tinted toward grey by the cover, the volumetric clouds' coverage and density parameters set between the
settings' ranges, a lightning flash. The controller writes the cloud parameters through a dynamic instance of each
layer's material, made on the first write, the authored values restored when nothing asks.

The engine's cloud material does not take 0..1: its instance authors `Cloud_GlobalCoverage` -0.2 and
`Cloud_GlobalDensity` 0.008. The ranges that read right on `VoxelDemo` are coverage -0.35 (a few fair-weather cumulus)
to 0.3 (a closed ceiling) and density 0.006 to 0.12 (a dark ceiling); the defaults hold them.

`MPC_Weather` holds the front and the local reading for materials (the scalars named in the instructions file, the wind
direction as a vector), and the mirror table writes the same sources into other collections (D64): VoxelWorlds' wind
inputs in P3, a pack's own collection with no edit to the pack.

Each local player gets an `AWorldWeatherViewer` that follows the camera and carries a local fog volume (D62): the
weather's fog about the viewer, on a planet too, where the clock hides the height fog; `ww.Viewer.Fog 0` turns it off.

## 6. Results

### P0 (2026-11-06): the plugin and the core

The repository, the submodule, the module above, the seven default presets, the tags, the pure math, the subsystem, the
state actor, the two provider interfaces, the six console commands, the eight tests. No effect yet: `ww.Weather` shows
the front changing on the server and on a client. The default table lives in code (`FillDefaults`); P1's content script
bakes it into an asset the settings name.

### P1 (2026-11-06): the sky

The modifier slot on the clock controller (WorldClockPlugin; `WorldClock.Lighting.Modifiers`), the subsystem's sky tick,
`MPC_Weather` and the mirror table, the viewer actor with the local fog volume, `Scripts/configure_weather.py` (the
collection and `DA_WeatherPresets` in the plugin's Content, named by `Config/DefaultGame.ini`), `Scripts/qa/weather_shots.py`
(PIE at noon, the clock stopped, a shot per preset with the frame's luma and the sky-write cost; `--set` overrides the
settings live for tuning). Seen on `VoxelDemo` with the camera at the pawn: Clear a blue sky with cumulus, Overcast a
broken grey sky with the ground dimmer (luma 97 to 94 of 255), Storm a closed grey ceiling with haze and the ground
at 92, Fog cumulus through a white haze (the height fog's density scale); a strike lifts the whole frame by 16. On `PlanetTest`
from 30 degrees north a storm dims the ground (luma 138 to 115) but no fog shows: the local fog volume renders nothing in this
project, in PIE or in the editor world, the engine's own actor included (G134, `Documentation/FINDINGS.md`); the viewer keeps
its component so a fix lights it with no plugin change, and until then a planet has no weather fog. The sky, collection and
mirror writes cost 0.013-0.032 ms a tick (0.02 average), under the plan's 0.05.
