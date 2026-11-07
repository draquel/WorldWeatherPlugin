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
| `IWorldWeatherClimateProvider`, `IWorldWeatherShelterProvider` | What is world-specific: the climate and frame at a place, the seed; shelter a trace cannot see (VoxelWorldPOI's `UPOIWeatherBridge` implements both) |
| `AWorldWeatherViewer` | Per local player, following the camera: the local fog volume (P1), the precipitation component (P2) |
| `UWorldWeatherPrecipitationComponent` | The particles (a Niagara system per type) and the occlusion capture (an orthographic depth capture into `RT_WeatherOcclusion`) |
| `WorldWeatherEditor` (module), `UWorldWeatherContentLibrary` | Editor only: builds the precipitation systems from a pack's (the sprite material, local space) for the content script |
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

## 6. Climate, shelter and precipitation (P2)

The climate and the shelter come in through VoxelWorldPOI's `UPOIWeatherBridge` (D60): once the chunk manager and the
weather exist it sets itself as both providers. The climate at a place is VoxelWorlds' `UVoxelChunkManager::GetClimateAt`
(the biome's three fields, shaped by the latitude and the lapse rate as generation shapes them), the temperature and
moisture mapped from about [-1, 1] to 0..1, the altitude, the local up, under the sea; the world seed folds into the
schedule. The shelter: exposure 0 inside a stamped dungeon's bounds and inside a claim tagged `Claim.Shelter`; anywhere
else the trace cone. So one front is rain at a planet's equator and snow at its poles and on its peaks, and the
schedule draws Snow where the reference point is cold.

The precipitation (D63) is the viewer's `UWorldWeatherPrecipitationComponent`, fed `GetConditionsAt(camera)` every tick:
the settings' `RainSystem` (rain and sleet) or `SnowSystem` (snow) activated while the rate at the view is over
`PrecipitationActivateAbove`, deactivated (the particles dying out) otherwise; the component rotated so its local -Z is
the fall direction `-up x FallSpeed + wind` (the systems are local-space, so the box and the velocities lean with it);
`User.Rate`, `User.Wind`, `User.Up` set when a system exposes them. The material fades with `MPC_Weather.LocalPrecipitation`.

The occlusion: an orthographic scene-depth capture `OcclusionHeight` above the camera, looking down the local up over
`2 x OcclusionExtent`, every `OcclusionFrames` frames into `RT_WeatherOcclusion` (lighting, fog, post-processing,
translucency and particles off; the ortho planes manual, 0 to the world's far). Its frame goes into the collection
(`OcclusionOrigin`, `OcclusionU`, `OcclusionV`, `OcclusionForward`; `UWorldWeatherMath::OcclusionFrame`), and the
precipitation material computes each pixel's capture UV and depth (`OcclusionCoords`' sums in a Custom node) and hides
it under the captured depth: dry under a roof, a canopy, a cave's ceiling, with no geometry knowledge; the trace cone
stays the gameplay exposure. With the capture off (`ww.Viewer.Occlusion 0`, or no target named) the particles fade with
the viewer's traced exposure instead.

The content: the editor's Python reaches no Niagara emitter or renderer, so the plugin's editor module
(`UWorldWeatherContentLibrary::BuildPrecipitationSystem`) gives a duplicated system's sprite renderers the material and
sets its emitters to local space; `Scripts/configure_weather.py` duplicates the Spruce pack's `NS_Rain` and `NS_Snow`
into the plugin's Content as `NS_WeatherRain` and `NS_WeatherSnow` (tracked; the pack untouched), makes the render
target and `M_WeatherPrecipitation` (unlit translucent: a streak from the sprite's UV, the particle colour, the fade,
the occlusion), and calls the library.

## 7. Results

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
from 30 degrees north a storm dims the ground (luma 138 to 115) but no fog showed: the local fog volume rendered nothing
(G134, fixed below). The sky, collection and mirror writes cost 0.013-0.032 ms a tick (0.02 average), under the plan's 0.05.

### G134 (2026-11-07): the local fog volume

The viewer's local fog volume drew nothing, in PIE or in the editor world, the engine's own `ALocalFogVolume` included, with
every project setting and renderer cvar ruled out. The editor's `DumpGPU` showed the renderer doing its whole job: the
culling pass listed the volume in every tile, the tiled draw ran, and the scene colour was byte-identical before and after
it; `r.LocalFogVolume.TileDebug 1` painted every tile. The pixel shader's contribution was the identity because the engine
combines the volume's radial and height densities as coverages, `T = 1 - (1 - TR) * (1 - TH)`
(`LocalFogVolumeCommon.ush`, `LocalFogVolumeEvaluateAnalyticalIntegral`): with the height extinction at 0 the
transmittance is 1 whatever the radial extinction. The viewer zeroed `HeightFogExtinction` in its constructor ("the sphere
does it"), and every probe, the engine actor's included, copied that; a height extinction of 1 fogged the view at once. The
volume now carries the weather's density in the height term (`LocalFogExtinctionMax`, per metre, uniform across the sphere
at falloff 1; default 0.02, about 50 m of visibility) and a dense radial term (100) as the sphere's soft edge, both 0 when
the front has no fog. Seen in PIE on `VoxelDemo` with a 500 m sphere about the camera: 0.02 hides the far shore and the
trees fade within 50 m, 0.005 is a light haze. `weather_shots.py --map=planet "--pre=vox.TPLatLon 30 20" --presets=Clear,Fog`:
on `PlanetTest` the Fog preset now greys the sky (luma 74 to 111) and dims the ground (145 to 119) with the trees fading
into it, the GPU frame 4.00 to 4.24 ms; on `VoxelDemo` the sky goes 103 to 120 (the height fog's scale and the volume
together). Found and fixed 2026-11-07 (the parent's `Documentation/FINDINGS.md`, G134 closed).

### P2 (2026-11-06): climate, shelter and precipitation

VoxelWorlds' `GetClimateAt` (its PR), VoxelWorldPOI's `UPOIWeatherBridge` and the precipitation above. Seen in PIE
(`Scripts/qa/weather_shots.py`, the survey camera now in the local frame): on `VoxelDemo` the bridge answers
(`ww.Weather`: climate provider `POIWeatherBridge`, T 0.52 M 0.47 at the spawn), Rain draws rain at 5.6 C, Storm heavier
rain, the Snow preset sleet at 0.6 C (the rain system); the streaks fall over the lake and the open ground and are cut
against the hillside and under the spruce canopy (the capture's depth; `ww.Viewer.Occlusion 0` draws them everywhere).
Inside a stamped dungeon (`vox.TPRoom Entrance`) the bridge gives exposure 0 and the capture, looking down through the
ground, hides every particle. On `SmoothTerrain_Planet` the same Storm is rain at 20 N (21.5 C, the streaks along the
radial up, clipped at the grass with the capture on, whole with it off) and snow at 78 N (-8.8 C). The GPU frame on
`VoxelDemo` under the storm (1280 x 722): 5.20 ms with the particles off, 5.36 with the particles and no capture, 5.61 with
the capture every 4 frames (the default), 6.00 every frame: about 0.4 ms for both at the defaults, under the plan's 0.5.
The pack's emitters simulate on the CPU (`DescribeSystem`); a GPU emitter is a content change for later. The capture
runs the full renderer at 512 px for a depth: its cost is the renderer's, not the resolution's.
