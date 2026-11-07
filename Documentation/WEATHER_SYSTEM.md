# The weather system

> The reference document of WorldWeatherPlugin, with `.claude/instructions.md`. The design record is the parent
> project's `Documentation/WEATHER_SYSTEM_PLAN.md` (the question, the survey, the architecture, D58-D66, P0-P6; frozen
> 2026-11-07). The results per phase are section 10, what is left section 11; the words are the parent's
> `Documentation/GLOSSARY.md` (Weather), the console its `EDITOR_CONSOLE_GUIDE.md` section 8, the hand-run checks QA
> area F12.

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

The mirror table (`UWorldWeatherSettings::MirrorTable`, `FWorldWeatherMirrorEntry`): each entry is a source (an
`EWorldWeatherMirrorSource`), a scale and an offset (the value written is source x scale + offset; a vector source,
`WindDirection`, ignores both) and one of two target kinds, resolved at `Initialize`:

- **A collection parameter** (`Collection`, `Parameter`): written through the world's collection instance every tick
  (D64). A collection that is not installed is skipped with a Log line.
- **An instance group** (`InstanceGroup`, `Parameter`; the foliage and bolt plan's D67): the entry names a group
  instead of a collection, and the parameter is written on every material instance a game has registered under the
  group, every tick (`RegisterMirrorInstance(Group, Instance)` / `UnregisterMirrorInstance`; `GetMirrorInstanceCount`).
  The instances are held weakly, an instance gone is dropped at the next write, registering twice is one registration,
  and an unregistered instance keeps its last value. For a material whose parameters are per instance and read no
  collection: the Megascans foliage master's `Wind Intensity`, through the scatter's dynamic instances, which
  VoxelWorldPOI's `UPOIWeatherBridge` registers under `FoliageWind` (section 7). `ww.Weather` prints each group with
  its live instance count. Test `WorldWeather.Subsystem.MirrorInstances`.

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

## 7. Materials (P3)

The weather reaches materials through collections only (D64). `MPC_VoxelMaterial` (VoxelWorlds') gained the scalars
`WindSpeed`, `WindIntensity`, `GustFactor`, `Wetness`, `SnowCover`, `SnowFreezeLevel`, `LocalCold`, `RippleStrength`
and the vector `WindDirection`; the mirror table in `Config/DefaultGame.ini` fills them every tick, and VoxelWorlds'
materials read them with no knowledge of this plugin. Three sources were added for it: `WindAzimuth` (the front's
azimuth in degrees, for a pack's scalar wind direction, turned into the pack's convention by the entry's Scale and
Offset), `FreezeLevel` (the climate temperature, 0..1, at which it snows: `SnowBelowC` inverted through the settings'
linear map, `UWorldWeatherMath::FreezeLevel`) and `ColdFactor` (the cold at the local viewer, 1 at or under `SnowBelowC`,
0 a sleet band above). A mirror entry naming a collection that is not installed (the Spruce pack's `MPC_Global`) is
skipped with a log line, not a warning.

What the materials do with them (VoxelWorlds' `MATERIAL_SYSTEM.md`, `SCATTER_SYSTEM.md`, `WATER_SYSTEM.md`): the
terrain masters darken and smooth the ground with `Wetness` and whiten it with `SnowCover` where the vertex's biome sits
below the freeze level (the ends of the biome's temperature range from a per-world lookup texture the renderer builds,
`FVoxelBiomeLookup`, against `SnowFreezeLevel`; a biome spanning most of the climate, alpine or coast, takes the
viewer's `LocalCold` instead: one global front, snow on the tundra, the taiga and the cold slopes about a player in the
cold, rain on the meadow, at every LOD and on the far field alike), on the faces that look along the local up; the cross billboards' master leans
the quad with `WindDirection` by `WindIntensity` and `GustFactor`; the sea's normal ripples along the wind by
`RippleStrength`. The Spruce pack's foliage moves through its own `MPC_Global` (`Wind Intensity`, `Wind Direction`),
two mirror entries, no edit to the pack. The Megascans foliage master (`M_MS_Foliage_Material`) keeps its wind per
instance (the engine's `SimpleGrassWind` behind the static switch `EnableGrassWind`, the scalars `Wind Intensity`,
`Wind Height`, `Wind Speed`) and reads no collection; since F1 of the foliage and bolt plan (D67) it follows the weather
through an instance group: the content script writes a material instance per plant with the switch on
(`MI_<plant>_Wind`), the scatter draws each Megascans definition through one dynamic instance of it
(`bDynamicMaterials`, VoxelWorlds' `SCATTER_SYSTEM.md` "Dynamic materials"), the bridge registers those under
`FoliageWind`, and the mirror entries write `Wind Intensity` (and `Wind Speed`) from the front's wind (section 10, F1).
The Megaplants' baked static meshes have no wind at all; their skinned assemblies are the scatter rendering slice the
plan names (F2, F3), not a material input.

## 8. The player (P4)

Three more components on the viewer, fed the conditions at the camera every tick (D65 for the sound):

- **`UWorldWeatherAmbienceComponent`**: three looping two-dimensional sources, light rain, heavy rain and wind (the
  settings' `RainLightSound`, `RainHeavySound`, `WindSound`: any `USoundBase`; a MetaSound source also gets the float
  inputs `Rate`, `Wind`, `Exposure`), their volume from the rate (light up to half, heavy past it; snow a third as loud)
  and the wind's intensity with its gusts, both quieter (`InteriorVolumeDrop`) and low-passed (`InteriorLowPassHz`) by
  the viewer's shelter; started with a fade when they have volume, faded out when not. Thunder is one-shot: the
  lightning component queues it with the strike's distance, and it plays after `Distance / SpeedOfSound`, quieter to
  `ThunderMaxDistance`. `ww.Viewer.Audio 0` silences it. The sounds are placeholders: a generated rain loop and thunder
  clap (`Scripts/make_weather_sounds.py`, imported by the content script as `SW_WeatherRain` and `SW_WeatherThunder`)
  and the animation sample's `Starter_Wind05`; no submix or modulation (none exists in the project).
- **`UWorldWeatherLensComponent`**: an unbound post-process component, priority over the clock's exposure and the
  dungeon's interior, carrying a dynamic instance of `LensMaterial` (`M_WeatherLens`, built by the content script: a
  Custom node over the scene colour after tonemapping) as its one blendable. The instance's `Drops` is the rate times the
  exposure times how far up the camera looks (none below the horizon, fewer in snow), `Frost` the cold factor at the view,
  `Wet` the ground's wetness; the inputs and the blend weight move with `FInterpTo` at `LensBlendSpeed`, and the weight is
  exactly 0 when nothing shows, so a clear day costs no pass. `ww.Viewer.Lens 0` turns it off.
- **`UWorldWeatherLightningComponent`**: bound to the subsystem's `OnLightning`; at a strike it spawns the settings'
  `LightningSystem` (`NS_WeatherLightning`, the bolt: the foliage and bolt plan's L1) at the strike's place, its +Z along
  the local up there, so the beam stands from the ground to `CloudBaseHeight` above it; adds a point light a little above
  the strike (`LightningLightLumens`, `LightningLightRadius`, `LightningLightColor`) that fades as the square of the time
  left over `LightningLightSeconds` (`FlashIntensity`, pure; the component ticks only while a light fades); and hands the
  thunder to the ambience with the strike's distance from the viewer. The flash in the sky is the clock controller's
  slot, as before. The bolt system is built by `configure_weather.py` through the editor module's
  `BuildLightningSystem`: an empty system gets a copy of the engine's `StaticBeam` emitter template, local space, the
  ribbon on `M_WeatherLightning` (unlit, additive, the particle colour bright), and the beam's end written into the
  emitter's baked module constants as `CloudBaseHeight` along +Z (`WorldWeather.Lightning.Bolt`).

The character plugin reads the weather for the pawn (`UVCMovementComponent`, behind `WITH_WORLD_WEATHER`: the plugin is
optional to it): `UpdateVoxelTerrainContext` reads `GetConditionsAt(pawn)` with the terrain context (every 0.1 s) and
keeps the wind, the exposure and the wetness; `CalcVelocity` adds the wind's drift (`ComputeWindDrift`, a pure static
beside the world edge's push-back: the wind x `WindPushFactor` x the exposure, scaled by the pawn's own speed over
its max speed, or full in the air) after the braking and the friction and takes the previous tick's off before them,
so a standing pawn idles in any wind, a pawn walking into the wind is slowed and one walking with it helped, and a
fall is carried (flying exempt; the standing drift of the first P4 was dropped on 2026-11-07), and the ground
friction loses `Wetness x WetGripFactor` of itself.
Both run inside the movement simulation, so the client's prediction and the server agree (the sync point is the same on
both). Test `VoxelCharacter.Movement.WindPush`.

The dedicated-server PIE test (`VoxelEngine.Replication.Framework`) gained a row: the server sets a storm, and the client
reads the same sync point serial, preset and front within a sync interval. The hand-run QA plan gained an area, F12.

## 9. Reactions (P5)

What else in the game reads the weather (2026-11-07). Each piece is a consumer in its own plugin, bound by tag or by
`GetConditionsAt`; this plugin gained nothing in P5.

- **The save** (VoxelWorldSave): `FVoxelWorldSaveDocument.Weather` is `FWorldWeatherSaveState` (the sync point, the
  schedule's place, the lock) beside `.Clock`; `BuildDocument` exports it through `ExportState`, `ApplyDocument` imports
  it after the clock (the point is in clock seconds) when the document has one (a document from before the field has
  no front tag and leaves the schedule alone). A loaded storm fires no change event (`ImportState` re-primes the
  edges). `VoxelWorldSave.Document.RoundTrip` covers the field.
- **The surface NPCs** (VoxelWorldPOI): a table entry's `bHidesInStorms` (the demo's deer) holds its creatures where
  they are while the current preset is `Weather.Preset.Storm`: `UPOISurfaceNPCSubsystem` checks the preset every pass
  (0.5 s) and pushes `AVCNPCCharacterBase::SetWeatherHold` to every alive creature of such an entry when the storm
  starts or ends; a creature spawned into a storm holds from the start. In the AI, `FVCNPCAIDecisionInput::
  bHoldsForWeather` turns a prey's roaming and patrolling into Idle; a threat still makes it flee; hostiles ignore it
  (`VoxelCharacter.AI.WeatherHold`).
- **The campsite's fire** (VoxelWorldPOI): visual only, every machine. `APOICampsite` reads the conditions at its light
  every half second and sets the light to `LightIntensityLumens x FireGutter(rate x exposure)`, a steady loss of
  `RainGutterFactor` (0.65) under full rain plus a flicker that grows with the rain; under a roof the exposure keeps it
  steady (`VoxelWorlds.POI.Campsite.FireGutter`).
- **The character's states** (VoxelCharacterPlugin): the movement component's weather read (P4) also keeps the air
  temperature and a wet level, `AdvanceWetLevel`: the rain reaching the pawn (rate x exposure, snow at a third) soaks
  it over `WetSoakSeconds` (12), a swim at once, and nothing falling dries it over `WetDrySeconds` (90). Every half
  second the server (`AVCCharacterBase::UpdateWeatherEffects`) sets `State.Weather.Wet` (on at a wet level of 0.6,
  off under 0.2) and `State.Weather.Cold` (on at 0 C, off over 2 C) as replicated loose tags on the ability system,
  read back from it rather than kept on the body (a player's lives on the player state and outlives a respawn). Wet
  halves the stamina's regeneration (`WetStaminaRegenMultiplier` in `TickStamina`); cold applies
  `UVCWeatherColdEffect`, an infinite effect taking 0.15 off the move speed multiplier, removed by class when the
  cold lifts. A corpse or a downed character loses both. The vitals widget shows "Soaked", "Cold" or "Soaked and cold"
  from the tags' events; a change logs once, `vc.Weather.Log 1` the level and the temperature every half second
  (`VoxelCharacter.Movement.WetLevel`).
- **Music**: nothing. No music system exists; `OnWeatherChanged` and the tags are the hook a later one binds.

## 10. Results

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

### P3 (2026-11-07): the material inputs

The collection's inputs, the mirror table, VoxelWorlds' biome lookup and the material edits of section 7; the Spruce
pack's foliage through its own collection. Seen in PIE (`weather_shots.py`, now with `--post` to run the clock forward
and `--local` for a planet's local noon): on `SmoothTerrain_Planet` at 65 N on taiga ground under a Snow front run two
game hours (snow cover 0.95, -1.6 C) the dark soil goes pale and glossy with snow, while the same front at 20 N
(17.5 C, rain, wetness 1.0) darkens and glosses the meadow and whitens nothing; on `VoxelDemo` an hour of Rain darkens
the ground, the lake ripples with the wind (a storm's 900 uu/s against a calm) and the Spruce trees move with the wind
set by `ww.Wind 90 800` (the tree region of two frames differs four times more than the rock beside it). The cross
billboards' master compiles, but no billboard stood near the cubic survey camera, so its lean is not seen yet. The
masters compile on the next editor launch (the terrain masters take minutes, which blocks an editor that recompiles
them in a slow task: the script runs headless). Two lessons: a vector parameter's main material output is three
components, its alpha a separate pin; and a biome's mid temperature is no guide for a biome spanning the climate
(Alpine, Coast), hence `LocalCold`.

### P4 (2026-11-07): the player

The ambience, lens and lightning components of section 8, the character plugin's wind drift and wet grip, the
dedicated-server test's `WeatherSync` step and QA area F12. Seen in PIE on `VoxelDemo`: under `ww.Wind 90 800` (756 uu/s
at the pawn once the 30 s blend is through) a standing pawn drifts east at 75 to 93 uu/s, sampled each second over ten
(the standing drift was dropped after P5, 2026-11-07: the drift scales with the pawn's own motion, full in the air)
(`vox.Debug.Capture`'s speed 72 to 94), and not at all in a calm; under a Storm the rain loops play at 0.58 and 0.42
(light and heavy, rate 0.72) with the wind loop at the storm's full, and inside the Tiled dungeon's entrance
(`vox.TPPOI Tiled; vox.TPRoom Entrance`) they drop to 0.21 and 0.29 under a 600 Hz low-pass with the lens off; a strike
at 3,000 uu (`ww.Strike 3000`) queues its thunder and plays it 0.09 s later; the lens shows drops 0.37 looking up into
the fall and 0.00 looking down, frost 1.0 at -5.8 C (and on `SmoothTerrain_Planet` at 65 N under a Snow front) and
0.0 at 6 C; with the lens on against off in the same storm frame the edges are paler by 46 to 66 per channel and the
centre unchanged (the frost vignette), the drops a scatter of refracting beads looking up. The lens material's first
cut never rendered: a custom node's `SceneTextureLookup` is undeclared unless a SceneTexture node is in the graph,
the material fails to compile and the editor uses the default one with one Warning in the log (the `ww.Weather`
line still shows the weight: it is the component's, not proof the shader runs), and that default drew a soft brown
frame at the screen's edges, chased as a frost bug for a while; the graph now carries a SceneTexture node
(PostProcessInput0) feeding the custom node its `Scene`. The first cut of the push, a per-tick lean, never moved a standing pawn: the braking of the next
tick ate it; hence the drift taken off before the braking and put back after it. Two more lessons: a test world made
with `CreateWorld` has no game mode, so nothing dispatches `BeginPlay` to its actors (`NotifyBeginPlay` on the world
settings does, `EndPlay` on the world before the teardown); and a unity build merges two files' anonymous namespaces,
so a helper copied into a second file is a redefinition (`UWorldWeatherSettings::LoadIfPresent` is the one copy now).
The suite is 20/20 with the weather, clock and character movement roots; the in-editor gate 27/27 with the parity
lines unchanged. The wet grip is not measured here (F12-08 is the hand-run row).

### P5 (2026-11-07): the reactions

The save field, the deer's storm hold, the campsite fire's guttering and the character's wet and cold states of section
9. Seen in PIE on `VoxelDemo` at the campsite (`vox.TPPOI Camp`; the player start is in the lake, where the pawn swims
and is soaked at once, and the shore's shallows count as water to the terrain context, so the soak-at-once is tied to
swimming, not to that flag): 45 s of rain soak the pawn (the state logs "soaked"), and under a clear sky the level
falls 1.00, 0.78, 0.55, 0.33 at 20 s steps, "dry" at 0.10 and 0.00 at 100 s; a Rain front on the demo cools the air by
5 C and a Snow front to -8, so the cold state comes and goes with them (on at 0 C, off at 2 C). Three deer spawned ad
hoc wander; a Storm front holds seven creatures (the three and the planned ones in range), a listed deer's state Idle,
and the Clear front releases them. The demo campsite's fire gutters under Rain (0.35 of the rain reaches it through its
cover) and burns steady again under Clear. A Storm saved (`vox.Save`), PIE stopped and started, comes back as the same
sync point (serial 11, its start at the same clock second) with no change event. The suite is 33/33 with the weather,
clock, character movement and AI, campsite and save roots. Seen on the way: a planned deer at the demo's cell (-2,1)
spawns at a point in deep water and is despawned the same pass, every pass (G139).

### L1 (2026-11-07): the bolt (the foliage and bolt plan, D69)

`NS_WeatherLightning` from the engine's `StaticBeam` emitter template through `BuildLightningSystem`, `M_WeatherLightning`
(the beam jagged by a lateral offset per segment along the ribbon, flickering, bright at the core, fading at both ends),
the strike light and the bolt's frame in the lightning component. Seen in PIE on `VoxelDemo` at the campsite at night
under a Storm, with `ww.Strike 1500 <bearing>` (the command gained a bearing so a strike can be put in front of a
camera) at the four cardinal bearings: a jagged white-blue bolt stands from the ground to the cloud base at each, the
near spruces lit by the strike light without whiting out (250,000 lumens with an inverse-square falloff, 12 m up the
bolt; 30,000 lumens 3 m up with a flat falloff had whited the whole frame), and the bolt, there at 0.3 and 0.5 s, is gone by 0.7 s (the particles live 0.25 s, the light fades over 0.4 s and the system is deactivated with it). The emitter's
module constants are reachable by name through the scripts' rapid-iteration parameter stores (`Beam End`,
`Lifetime`, `Loop Duration`), which is how the library tunes a template without the stack API; the template's loop
mode is not one of them, so the component also deactivates the system when its light has faded. The suite is 12/12
with the weather root. Seen on the way: a strike's point light at 3 m with a flat falloff lights the nearest trunks to
white; the place for it is up the bolt.

### F1 (2026-11-07): the Megascans foliage's wind (the foliage and bolt plan, D67)

Four pieces, each in the plugin that owns it. VoxelWorlds' scatter gained `FScatterDefinition::bDynamicMaterials`: the
renderer draws such a definition's override slots through one dynamic instance each, shared by its components in every
region, and the manager publishes them (`GetDynamicMaterials`, `GetAllDynamicMaterials`, `OnDynamicMaterialCreated`,
`OnDynamicMaterialReleased` at its shutdown). This plugin's mirror table gained the instance target (section 5:
`InstanceGroup`, `RegisterMirrorInstance`). VoxelWorldPOI's `UPOIWeatherBridge` binds the chunk manager's scatter and
registers its instances under `FoliageWind`. `Scripts/setup_smooth_terrain.py` writes `MI_<plant>_Wind` (parent the
plant's `MI_<plant>_2K`, `EnableGrassWind` on) in a `Wind` folder beside it in the pack, one for each slot on the
foliage master (the plant's and, where its billboard is on the same master, the billboard's: 41 instances for the
table's 33 Megascans plants), and draws the 76 Megascans definitions of each smooth world (of 108) through them with
`bDynamicMaterials`; `Config/DefaultGame.ini`'s entry writes `Wind Intensity` = `WindIntensity` x 0.3: 0 in a calm,
the pack's authored 0.15 at half of `StrongWindAbove` (300 uu/s), 0.3 in a strong wind or a storm. `Wind Speed` and
`Wind Height` keep the pack's 0.5: the sway's rate read right at every intensity, so no entry drives them.

Seen in PIE on `SmoothTerrain_Planet` at 20 N 20 E (a shallow marsh of swamp grass, reeds and marshcress;
`Scripts/qa/wind_shots.py`: the clock stopped, the pawn frozen, the precipitation off, a still camera at the pawn
pitched 30 degrees down, two shots a second apart per pair): `ww.Weather` lists `FoliageWind: 32` (the bridge bound
before the scatter made any, the delegate registered each). The mean absolute luma difference between the two shots
(of 255) is 0.95 over the frame in a calm (`ww.Wind 0 0`), 3.97 under the Windy front (800 uu/s, intensity 0.3), and
1.01 in the calm again; the pixels that moved more than 8 are 0.8 %, 11.0 % and 2.1 %. The windy pair's difference
image is the plants' silhouettes, blade by blade, and the calm pairs' only the shallow water's shimmer (its ripples)
and a faint upscaler outline: the plants stand still at zero wind and settle when it drops. The suites: the weather 13/13
(`WorldWeather.Subsystem.MirrorInstances` new), VoxelWorlds 498/498 (`VoxelWorlds.Scatter.DynamicMaterials`,
`VoxelWorlds.POI.WeatherBridge.ScatterWind` new); the in-editor gate 28/28 with `DynamicMaterials` added, the parity
lines unchanged. The wind instances' shaders compile on the first editor load after the script (the foliage master's
switch-on permutation per instance).

## 11. Later

What the plan named and left, and what the phases found, in one list (2026-11-07):

- **The Megaplants' skinned assemblies** (P3's slice): their baked static meshes have no wind; moving them is a
  scatter rendering slice of its own.
- **A lean against the wind**: the pawn's motion is slowed and helped, nothing shows it; the animation layer can read
  `UVCMovementComponent::GetWeatherWind`.
- **The campfire's flame**: the fire gutters as its light only; it has no flame particles to thin.
- **Music**: no music system; `OnWeatherChanged` and the tags are its hook.
- **G139**: a planned deer at the demo's cell (-2,1) spawns in deep water every pass (the surface NPC spawner's,
  seen in P5).
- **Not in scope**, as the plan said: seasons and a calendar, a planet-aware cloud material, geometry waves, rain
  filling pools or snow changing voxels, weather in dungeons, the front grid.
