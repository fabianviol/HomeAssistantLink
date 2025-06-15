# Home Assistant Link - Skyrim Mod

[![GitHub last commit](https://img.shields.io/github/last-commit/fabianviol/HomeAssistantLink)](https://github.com/fabianviol/HomeAssistantLink/commits/main)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## 🎮 Immerse Yourself in Skyrim with Dynamic Lighting!

Home Assistant Link is an SKSE plugin for Skyrim Special Edition (AE) that connects your game state to your Home Assistant smart home setup. This allows for dynamic lighting experiences, such as your smart lights changing color when you enter combat, your health gets low, or as the in-game time changes.

Currently, the plugin provides a robust foundation for this integration, with planned features to make the lighting scenarios fully customizable via the configuration file.

---

## ✨ Features

* **Seamless Integration:** Connects directly to your Home Assistant instance via its API.
* **Customizable Lights:** Configure a list of Home Assistant light entity IDs to control.
* **In-Game Console Logging:** Toggle a debug mode to see plugin messages directly in Skyrim's console.
* **Dedicated File Logging:** Detailed plugin activity is logged to `HomeAssistantLink.log` for easy troubleshooting.
* **Combat State Lighting:** Currently changes lights to **Red** when the player is in combat, and **Green** when out of combat. (More customizable triggers coming soon!)

---

## ⚡ Requirements

* **Skyrim Special Edition** (Anniversary Edition, 1.6.x)
* **SKSE64** (latest version for your Skyrim AE build)
* A running **Home Assistant** instance (e.g., Home Assistant OS, Docker, etc.)
* **Configured Smart Lights** in Home Assistant (e.g., Philips Hue, Tuya, Zigbee2MQTT, etc.)
* A **Long-Lived Access Token** from your Home Assistant user profile (needed for API access).

---

## 🚀 Installation

### 1. Download the Plugin
* Download the latest release of `HomeAssistantLink.dll` and `HomeAssistantLink.json` from the [Releases page](https://github.com/fabianviol/HomeAssistantLink/releases) (or build from source).

### 2. Manual Installation (Recommended via Mod Organizer 2 / Vortex)
1.  Navigate to your Skyrim Special Edition installation folder.
2.  Go into `Data/SKSE/Plugins`. If `SKSE` or `Plugins` folders don't exist, create them.
3.  Place `HomeAssistantLink.dll` and `HomeAssistantLink.json` directly into the `Data/SKSE/Plugins` folder.
    * **Example Path:** `C:\Steam\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins\`

### 3. Verify SKSE Setup
* Ensure SKSE64 is correctly installed and running. You can check this by opening the Skyrim console (`~`) and typing `GetSKSEVersion`.

---

## ⚙️ Configuration (`HomeAssistantLink.json`)

Before launching Skyrim, you **must** configure the `HomeAssistantLink.json` file located in `Data/SKSE/Plugins`.

```json
{
  "HomeAssistant": {
    "Url": "[http://homeassistant.local:8123](http://homeassistant.local:8123)",  // REQUIRED: Your Home Assistant URL (e.g., [http://192.168.1.100:8123](http://192.168.1.100:8123))
    "Token": "YOUR_HA_LONG_LIVED_TOKEN",     // REQUIRED: Your Home Assistant Long-Lived Access Token
    "DebugMode": true                       // OPTIONAL: Set to 'true' to see plugin messages in Skyrim's console and more detailed file logs.
  },
  "Lights": [
    "light.your_first_light_entity_id",     // REQUIRED: A list of Home Assistant light entity IDs you want to control.
    "light.your_second_light_entity_id"     // Find these in Home Assistant Developer Tools -> States.
  ],
  "Scenarios": [
    // This section will be for defining custom light triggers and outcomes.
    // It's under active development and examples will be added soon!
  ]
}

HomeAssistant.Url: Replace with the full URL to your Home Assistant instance.
HomeAssistant.Token: Replace with your Long-Lived Access Token. Generate this from your Home Assistant user profile (Profile -> Long-Lived Access Tokens -> Create Token). Ensure this token has permissions to control your lights.
Lights: Populate this array with the exact entity_ids of the lights you want the mod to control. You can find these in Home Assistant under Developer Tools -> States (e.g., light.living_room_lamp).
🚀 Usage
Launch Skyrim through SKSE64.
The plugin will initialize upon game load.
Check your Documents\My Games\Skyrim Special Edition\SKSE\HomeAssistantLink.log file for detailed activity from the plugin.
If DebugMode is enabled in your HomeAssistantLink.json, you will see some messages printed directly to the in-game console (open with ~).
Currently, the lights configured in your JSON will turn Red when you enter combat in Skyrim, and Green when you exit combat.
🚧 Building from Source (For Developers)
If you wish to build this plugin yourself or contribute:

Requirements:
Visual Studio 2019/2022 (Community Edition is fine)
vcpkg (correctly integrated with Visual Studio)
Git
Steps:
Clone the Repository:
Bash

git clone [https://github.com/fabianviol/HomeAssistantLink.git](https://github.com/fabianviol/HomeAssistantLink.git)
cd HomeAssistantLink
Ensure vcpkg.json is up-to-date:
Verify your vcpkg.json includes all necessary dependencies: commonlibsse-ng-ae, cpr, nlohmann-json, and spdlog.
Install Dependencies via vcpkg:
Bash

vcpkg install
This will fetch and build commonlibsse-ng-ae (and its dependencies like curl), cpr, nlohmann-json, and spdlog. This may take some time on the first run.
Open in Visual Studio:
Open the HomeAssistantLink.sln solution file in Visual Studio.
Build the Project:
Select the Release or Debug configuration (Debug is recommended for development).
Build the solution (Build -> Build Solution or Ctrl+Shift+B).
The HomeAssistantLink.dll and HomeAssistantLink.json (from a post-build copy step) will be generated in your project's x64\Debug or x64\Release folder. Copy these to your Skyrim Data/SKSE/Plugins directory.
💡 Planned Features / To-Do
Fully Customizable Scenarios: Implement the Scenarios section in HomeAssistantLink.json to allow users to define arbitrary triggers (e.g., health thresholds, game time, weather, specific locations) and corresponding light outcomes (colors, brightness, effects).
More Trigger Types: Expand the range of game states that can act as triggers.
Configuration Reload: Allow reloading the HomeAssistantLink.json without restarting Skyrim.
Additional Home Assistant Services: Support more complex Home Assistant services beyond just light.turn_on.
🤝 Contributing
Contributions are welcome! Feel free to open issues for bug reports or feature requests, and pull requests if you want to contribute code.

📜 License
This project is licensed under the MIT License - see the LICENSE file for details.