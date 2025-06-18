Project Overview – Skyrim Game State to Smart Lighting Integration
Summary:
This project is a custom SKSE (Skyrim Script Extender) C++ plugin for Skyrim Special Edition. It exports live game state information (player position, time of day, proximity to fires, etc.) to Home Assistant via HTTP REST, in order to control real-world smart lights (e.g., LEDVANCE, Tuya, Philips Hue) in sync with what’s happening in the game.

Main Features:

Directional Lamp Mapping:
Lamps in the user’s real room are mapped to relative positions around the player (left, right, front, back, etc). Lamp color and brightness are dynamically determined based on nearby in-game light sources, player view direction, and the real-world positions of the lamps.

Day/Night Cycle Integration:
A “DayNightCycle” array is loaded from a JSON config. Each hour of the in-game day defines a color and brightness for ambient lighting. This is smoothly interpolated between keyframes.

Dynamic Blending:

If the player is near a fire/light source, lamps blend between “ambient” (from the day/night cycle) and the color/brightness of that fire/light, using proximity and directionality.

If in combat, torch equipped, or other scenario triggers, those override/blend accordingly.

Scenarios:
Special scenarios (e.g., “combat”, “torch equipped”) are also loaded from the config and can override normal day/night or dynamic lighting.

Interiors Handling:
In interior cells, ambient (day/night) lighting is ignored; lamps only react to local in-game light sources (fires, torches, etc).

Key Code Files:

GameState.cpp: Main export function. Blends dynamic and ambient/scenario lighting. Handles player state, blending logic, smoothing.

ConfigLoader.cpp/h: Loads JSON config, including DayNightCycle and scenarios, and exposes global config objects/arrays.

LampMapping.cpp/h: Maps in-game light sources and directions to real-world lamps using player view/yaw and lamp positions.

LightSmoother.cpp/h: Optional, smooths state changes to avoid flicker.

SkyrimLightsDB.cpp/h: Loads database of in-game light definitions (formIDs, color, radius, etc).

Logger.cpp/h: Handles logging to file and optional in-game/console messages.

Configuration:

All settings are in a JSON file (HomeAssistantLink.json), including Home Assistant URL/token, lamp positions, day/night keyframes, and custom scenarios.

Goal:
Make real-world lighting react to the events, position, and time of day in Skyrim, creating a highly immersive, responsive atmosphere.

