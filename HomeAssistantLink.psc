ScriptName HomeAssistantLink

Import Debug

Function ReloadConfig()
    bool success = HomeAssistantLink.ReloadConfig()
    if success
        Debug.Notification("HAL: Config reloaded!")
    else
        Debug.Notification("HAL: Failed to reload config!")
    endif
EndFunction