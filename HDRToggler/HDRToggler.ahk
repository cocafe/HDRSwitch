#Requires AutoHotkey v2.0
#SingleInstance Force

; ====== Special thanks to cocafe for HDRSwitch! ======

; === Tray Setup ===
A_IconTip := "HDR Toggler"
A_TrayMenu.Delete()
A_TrayMenu.Add("Toggle HDR", (*) => ToggleHDR())
A_TrayMenu.Add()
A_TrayMenu.Add("Exit", (*) => ExitApp())

OnMessage(0x404, TrayClickHandler)

TrayClickHandler(wParam, lParam, *) {
    ; 0x202 = WM_LBUTTONUP (left button released)
    if (lParam = 0x202) {
        ToggleHDR()
    }
}

; === Paths ===
iconOn := A_ScriptDir "\hdr-on.ico"
iconOff := A_ScriptDir "\hdr-off.ico"
hdrSwitchExe := A_ScriptDir "\HDRSwitch.exe"  ; Path to HDRSwitch.exe

; === Validate Executable ===
if !FileExist(hdrSwitchExe) {
    MsgBox "HDRSwitch.exe not found at:`n" hdrSwitchExe, "Fatal Error", 0x10
    ExitApp
}

; === Global HDR State Flags ===
isHDREnabled := false
isHDRSupported := false

; === Initialize ===
GetHDRState(0)
UpdateTrayIcon()

; === Hotkey: Ctrl + Alt + H to toggle HDR ===
^!h::ToggleHDR()

; ========== FUNCTIONS ==========

GetHDRState(monitorIndex := 0) {
    global hdrSwitchExe, isHDREnabled, isHDRSupported

    outputFile := A_ScriptDir "\hdrlog.txt"
    RunWait A_ComSpec ' /c "' hdrSwitchExe ' --list > "' outputFile '"', , "Hide"

    if !FileExist(outputFile) {
        isHDRSupported := false
        isHDREnabled := false
        return
    }

    output := FileRead(outputFile)
    isHDRSupported := false
    isHDREnabled := false

    Loop Parse, output, "`n", "`r" {
        line := Trim(A_LoopField)
        if line = "" || InStr(line, "Monitor Name")
            continue

        ; Match format: index, name, res, supported, enabled, depth, white level
        if RegExMatch(line, "^\s*(\d+)\s+\S+\s+\S+.*?\s+(\d)\s+(\d)", &m) {
            if (m[1] = monitorIndex) {
                isHDRSupported := (m[2] = "1")
                isHDREnabled := (m[3] = "1")
                return
            }
        }
    }
}

ToggleHDR(*) {
    global isHDRSupported, isHDREnabled, hdrSwitchExe
    GetHDRState(0)

    if !isHDRSupported {
        TrayTip "HDRToggler", "Monitor 0 does not support HDR.", 2
        return
    }

    RunWait hdrSwitchExe " --monitor 0 --toggle", , "Hide"
    Sleep 1000
    GetHDRState(0)
    UpdateTrayIcon()
}

UpdateTrayIcon() {
    global isHDREnabled
    TraySetIcon(isHDREnabled ? "hdr-on.ico" : "hdr-off.ico",, true)
    A_IconTip := "HDR is " (isHDREnabled ? "ON" : "OFF")
    
    if (isHDREnabled) {
        TrayTip "Let there be light! 💡", "HDR is ON", 1
    } else {
        TrayTip "In the SDR dungeons... 🕶️", "HDR is OFF", 3
    }
}
