; Modified by Shinyflvres, 2026-08-23. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md
;--------------------------------
; Include Modern UI

!include "MUI2.nsh"

;--------------------------------
; General Configuration

!define APP_VERSION "2.6.0"
!define APP_VERSION_META "2.6.0.0"
!define APP_NAME "SpaceSync"
!define LEGACY_APP_NAME "OpenVR-SpaceOverride"

!define INSTALL_DIR "$PROGRAMFILES64\${APP_NAME}"
!define LICENSE_FILE "../bin/LICENSE.txt"
!define FILES_DIR "../bin/"
!define DRIVER_DIR "driver"

Name "${APP_NAME}"
OutFile "${APP_NAME}_Installer.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "Software\${APP_NAME}\Main" ""
RequestExecutionLevel admin
ShowInstDetails show

VIProductVersion "${APP_VERSION_META}"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (c) 2026 Nyabsi. SpaceSync modifications Copyright (c) 2026 Shinyflvres. AGPL-3.0"
VIAddVersionKey /LANG=1033 "Comments" "SpaceSync is a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0), modified by Shinyflvres on 2026-08-23. See NOTICE.md."
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION_META}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"

;--------------------------------
; Variables

Var alreadyInstalled

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING

;--------------------------------
; Pages

!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!define MUI_PAGE_CUSTOMFUNCTION_PRE dirPre
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Language

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Functions

Function dirPre
    StrCmp $alreadyInstalled "true" 0 +2
        Abort
FunctionEnd

Function .onInit
    StrCpy $alreadyInstalled "false"

    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString"
    StrCmp $R0 "" done

    MessageBox MB_YESNOCANCEL|MB_ICONQUESTION \
        "${APP_NAME} is already installed.$\n$\nClick YES to Reinstall$\nClick NO to Remove$\nClick CANCEL to abort installation" \
        IDYES repair \
        IDNO remove
    Abort

    repair:
        StrCpy $alreadyInstalled "true"
        Goto done

    remove:
        ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR'
        MessageBox MB_OK "${APP_NAME} has been uninstalled."
        Delete "$INSTDIR\Uninstall.exe"
        RMDir "$INSTDIR"
        Quit

    done:
FunctionEnd


;--------------------------------
; Installer Section

Section "Install" SecInstall

    ; A running SteamVR keeps the driver DLL locked and would not load a newly registered driver.
    steamvrcheck:
    FindWindow $0 "" "SteamVR Status"
    StrCmp $0 0 steamvrclosed
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Please close SteamVR before installing ${APP_NAME}.$\n$\nClick Retry once SteamVR is closed." /SD IDCANCEL IDRETRY steamvrcheck
        Abort "SteamVR is still running."
    steamvrclosed:

    ; Close a running SpaceSync so its files can be replaced.
    nsExec::Exec 'taskkill /IM SpaceSync.exe /F'
    Pop $0
    Sleep 500

    ; Remove an old OpenVR-SpaceOverride install so there aren't two drivers.
    IfFileExists "$PROGRAMFILES64\${LEGACY_APP_NAME}\Uninstall.exe" 0 nolegacy
        DetailPrint "Removing previous OpenVR-SpaceOverride installation..."
        ExecWait '"$PROGRAMFILES64\${LEGACY_APP_NAME}\Uninstall.exe" /S _?=$PROGRAMFILES64\${LEGACY_APP_NAME}'
        Delete "$PROGRAMFILES64\${LEGACY_APP_NAME}\Uninstall.exe"
        RMDir /r "$PROGRAMFILES64\${LEGACY_APP_NAME}"
        DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_APP_NAME}"
        Delete "$SMPROGRAMS\${LEGACY_APP_NAME}.lnk"
    nolegacy:

    StrCmp $alreadyInstalled "true" 0 noupgrade
        DetailPrint "Cleaning previous installation..."
        ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR'
        Delete "$INSTDIR\Uninstall.exe"
    noupgrade:

    SetOutPath "$INSTDIR"

    File "${FILES_DIR}\LICENSE.txt"
    File "${FILES_DIR}\NOTICE.md"
	File "${FILES_DIR}\LICENSE"
	File "${FILES_DIR}\LICENSES"
	File "${FILES_DIR}\manifest.vrmanifest"
    File "${FILES_DIR}\SpaceSync.exe"
    File "${FILES_DIR}\openvr_api.dll"
    File "${FILES_DIR}\icon.png"
    SetOutPath "$INSTDIR\sound"
    File "${FILES_DIR}\sound\*.wav"

    SetOutPath "$INSTDIR\driver"
    File /r "${DRIVER_DIR}\*"

    WriteRegStr HKLM "Software\${APP_NAME}\Main" "" $INSTDIR
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""

    Var /GLOBAL vrRuntimePath
	nsExec::ExecToStack '"$INSTDIR\SpaceSync.exe" -openvrpath'
	Pop $0
	Pop $vrRuntimePath
	StrCmp $0 "0" runtimefound
		StrCpy $vrRuntimePath ""
		ReadRegStr $vrRuntimePath HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820" "InstallLocation"
	runtimefound:
	DetailPrint "VR runtime path: $vrRuntimePath"

	IfFileExists "$vrRuntimePath\bin\win64\vrpathreg.exe" 0 noruntime
		nsExec::ExecToLog '"$vrRuntimePath\bin\win64\vrpathreg.exe" adddriver "$INSTDIR\driver"'
		Pop $0
		DetailPrint "Driver registration exit code: $0"
		Goto runtimedone
	noruntime:
		MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR could not be found, so the SpaceSync driver was not registered.$\n$\nInstall SteamVR and start it once, then run this installer again." /SD IDOK
	runtimedone:

	SetOutPath "$INSTDIR"
	CreateShortCut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\SpaceSync.exe"
	nsExec::ExecToLog '"$INSTDIR\SpaceSync.exe" -installmanifest'
	Pop $0
	StrCmp $0 "0" +2
		DetailPrint "SteamVR app registration will be completed when SpaceSync first runs with SteamVR (code $0)."
	nsExec::ExecToLog '"$INSTDIR\SpaceSync.exe" -activatemultipledrivers'
	Pop $0
	StrCmp $0 "0" +2
		DetailPrint "activateMultipleDrivers will be set when SpaceSync first runs with SteamVR (code $0)."

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"

	nsExec::Exec 'taskkill /IM SpaceSync.exe /F'
	Pop $0

	SetOutPath "$INSTDIR"
	nsExec::ExecToLog '"$INSTDIR\SpaceSync.exe" -removemanifest'
	Pop $0

    Var /GLOBAL vrRuntimePath2
	nsExec::ExecToStack '"$INSTDIR\SpaceSync.exe" -openvrpath'
	Pop $0
	Pop $vrRuntimePath2
	StrCmp $0 "0" +3
		StrCpy $vrRuntimePath2 ""
		ReadRegStr $vrRuntimePath2 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820" "InstallLocation"
	DetailPrint "VR runtime path: $vrRuntimePath2"
	IfFileExists "$vrRuntimePath2\bin\win64\vrpathreg.exe" 0 +3
		nsExec::ExecToLog '"$vrRuntimePath2\bin\win64\vrpathreg.exe" removedriver "$INSTDIR\driver"'
		Pop $0

	SetOutPath "$TEMP"

    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\NOTICE.md"
	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\LICENSES"
	Delete "$INSTDIR\manifest.vrmanifest"
    Delete "$INSTDIR\SpaceSync.exe"
    Delete "$INSTDIR\openvr_api.dll"
    Delete "$INSTDIR\icon.png"
    Delete "$INSTDIR\sound\*.wav"
    RMDir "$INSTDIR\sound"
    RMDir /r "$INSTDIR\driver"

    DeleteRegKey HKLM "Software\${APP_NAME}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"

    RMDir "$INSTDIR"

SectionEnd
