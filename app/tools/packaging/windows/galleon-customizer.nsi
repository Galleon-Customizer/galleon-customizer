; SPDX-FileCopyrightText: 2026 Paul Woisard
; SPDX-License-Identifier: GPL-3.0-or-later
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef VERSION
!error "VERSION must be supplied"
!endif
!ifndef STAGING_DIR
!error "STAGING_DIR must be supplied"
!endif
!ifndef OUTPUT_FILE
!error "OUTPUT_FILE must be supplied"
!endif
!ifndef UNINSTALL_MANIFEST
!error "UNINSTALL_MANIFEST must be supplied"
!endif

!ifndef PAYLOAD_MANIFEST
!error "PAYLOAD_MANIFEST must be supplied"
!endif

Name "Galleon Customizer ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Galleon Customizer"
InstallDirRegKey HKCU "Software\Galleon Customizer\Installer" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetFont "Segoe UI" 9
BrandingText "Galleon Customizer - Galleon Customizer"
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Galleon Customizer"
VIAddVersionKey /LANG=1033 "FileDescription" "Galleon Customizer Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "2026 Paul Woisard"

!define MUI_ICON "${__FILEDIR__}\assets\galleon-customizer.ico"
!define MUI_UNICON "${__FILEDIR__}\assets\galleon-customizer.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${__FILEDIR__}\assets\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${__FILEDIR__}\assets\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${__FILEDIR__}\assets\welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\galleon-customizer.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
LangString CoreLabel ${LANG_ENGLISH} "Galleon Customizer (required)"
LangString CoreLabel ${LANG_FRENCH} "Galleon Customizer (obligatoire)"
LangString DesktopLabel ${LANG_ENGLISH} "Desktop shortcut"
LangString DesktopLabel ${LANG_FRENCH} "Raccourci sur le bureau"
LangString StartupLabel ${LANG_ENGLISH} "Start galleon-customizerd and Galleon Customizer when signing in"
LangString StartupLabel ${LANG_FRENCH} "Démarrer galleon-customizerd et Galleon Customizer à l'ouverture de session"
LangString StopError ${LANG_ENGLISH} "Galleon Customizer could not stop cleanly. Close Galleon Customizer and galleon-customizerd, then retry."
LangString StopError ${LANG_FRENCH} "Galleon Customizer n'a pas pu s'arrêter proprement. Fermez Galleon Customizer et galleon-customizerd, puis réessayez."


Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Galleon Customizer requires Windows x64."
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP "Galleon Customizer requires Windows 10 or later."
        Abort
    ${EndIf}
    SetShellVarContext current
FunctionEnd
Function un.onInit
    SetShellVarContext current
FunctionEnd

!macro StopApplications PREFIX MODE
Function ${PREFIX}StopApplications
    InitPluginsDir
    File /oname=$PLUGINSDIR\stop-galleon.ps1 "${__FILEDIR__}\stop-galleon.ps1"
    ClearErrors
    ExecWait '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\stop-galleon.ps1" -InstallDir "$INSTDIR" -Mode ${MODE}' $0
    IfErrors shutdown_failed
    ${If} $0 == 0
        Return
    ${EndIf}
shutdown_failed:
    IfSilent +2
    MessageBox MB_ICONSTOP "$(StopError)"
    SetErrorLevel 1
    Abort
FunctionEnd
!macroend
!insertmacro StopApplications "" "Prepare"
!insertmacro StopApplications "un." "Stop"

Section "$(CoreLabel)" Core
    SectionIn RO
    Call StopApplications
    SetOutPath "$INSTDIR"
    File /r "${STAGING_DIR}\*"
    File /oname=install-payload.txt "${PAYLOAD_MANIFEST}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    Delete "$SMSTARTUP\Galleon Customizer.lnk"
    Delete "$SMSTARTUP\Galleon Customizer.lnk"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Galleon Customizer"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GalleonBosun"
    CreateDirectory "$SMPROGRAMS\Galleon Customizer"
    CreateShortCut "$SMPROGRAMS\Galleon Customizer\Galleon Customizer.lnk" "$INSTDIR\bin\galleon-customizer.exe"
    CreateShortCut "$SMPROGRAMS\Galleon Customizer\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\Galleon Customizer\Installer" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "DisplayName" "Galleon Customizer"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "Publisher" "Paul Woisard"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "DisplayIcon" "$INSTDIR\bin\galleon-customizer.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer" "NoRepair" 1
SectionEnd
Section /o "$(DesktopLabel)" Desktop
    CreateShortCut "$DESKTOP\Galleon Customizer.lnk" "$INSTDIR\bin\galleon-customizer.exe"
SectionEnd
Section "$(StartupLabel)" Startup
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Galleon Customizer" '$\"$INSTDIR\bin\galleon-customizerd.exe$\"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GalleonBosun" '$\"$INSTDIR\bin\galleon-customizer.exe$\" --minimized'
SectionEnd
Section "Uninstall"
    Call un.StopApplications
    !include "${UNINSTALL_MANIFEST}"
    Delete "$SMPROGRAMS\Galleon Customizer\Galleon Customizer.lnk"
    Delete "$SMPROGRAMS\Galleon Customizer\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Galleon Customizer"
    Delete "$DESKTOP\Galleon Customizer.lnk"
    Delete "$SMSTARTUP\Galleon Customizer.lnk"
    Delete "$SMSTARTUP\Galleon Customizer.lnk"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Galleon Customizer"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GalleonBosun"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Galleon Customizer"
    DeleteRegKey HKCU "Software\Galleon Customizer\Installer"
    Delete "$INSTDIR\install-payload.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
