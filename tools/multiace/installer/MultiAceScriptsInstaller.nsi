; MultiACE Scripts Installer
; Installs the post_process_virtual_toolheads.exe helper to the fixed path
; Orca MultiACE edition's shipped presets expect it at. Deliberately
; separate from Orca's own installer (see src/CMakeLists.txt's note on why) -
; this is its own small, standalone install/uninstall unit.

Unicode true

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "FileFunc.nsh"

Name "MultiACE Scripts Installer"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\Orca Slicer - MultiACE edition"
InstallDirRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "InstallLocation"
RequestExecutionLevel admin

!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_ABORTWARNING

Var Dialog
Var LogoCtl
Var TitleCtl
Var SubtitleCtl
Var LogoBitmap

; ---------------------------------------------------------------------------
; Custom welcome page: Orca splash logo on top, "MultiACE Scripts Installer"
; directly underneath it, centered.
Function CustomWelcomeCreate
    nsDialogs::Create 1018
    Pop $Dialog
    ${If} $Dialog == error
        Abort
    ${EndIf}

    ; Extract the bundled logo bitmap to the plugins temp dir so it can be
    ; loaded at runtime (embedded via File in .onInit below).
    ${NSD_CreateBitmap} 75u 0u 150u 154u ""
    Pop $LogoCtl
    ${NSD_SetStretchedImage} $LogoCtl "$PLUGINSDIR\splash_logo.bmp" $LogoBitmap

    ${NSD_CreateLabel} 0 158u 100% 20u "MultiACE Scripts Installer"
    Pop $TitleCtl
    CreateFont $1 "$(^Font)" "14" "700"
    SendMessage $TitleCtl ${WM_SETFONT} $1 1

    ${NSD_CreateLabel} 10u 182u 280u 40u "Installs the ACE color-swap post-processing helper that Orca Slicer - MultiACE edition's printer profiles expect. Requires Orca Slicer - MultiACE edition to already be installed."
    Pop $SubtitleCtl

    nsDialogs::Show
FunctionEnd

!define MUI_PAGE_CUSTOMFUNCTION_SHOW CustomWelcomeCreate
!insertmacro MUI_PAGE_WELCOME

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
    InitPluginsDir
    File "/oname=$PLUGINSDIR\splash_logo.bmp" "${LOGO_BMP}"

    ; Refuse silently if Orca MultiACE edition isn't actually installed here -
    ; this helper is meaningless without it.
    ${If} ${FileExists} "$INSTDIR\Orca Slicer - MultiACE edition.exe"
    ${Else}
        MessageBox MB_YESNO|MB_ICONEXCLAMATION \
            "Orca Slicer - MultiACE edition was not found at:$\n$INSTDIR$\n$\nInstall it there anyway, or Cancel to pick the correct folder on the next page?" \
            IDYES proceed
        Abort
        proceed:
    ${EndIf}
FunctionEnd

Section "MultiACE post-processing script" SecMain
    SetOutPath "$INSTDIR\resources\multiace"
    File "${HELPER_EXE}"

    WriteUninstaller "$INSTDIR\resources\multiace\Uninstall MultiACE Scripts.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "DisplayName" "MultiACE Scripts"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "UninstallString" "$INSTDIR\resources\multiace\Uninstall MultiACE Scripts.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "Publisher" "Mnemonic3D"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "DisplayIcon" "$INSTDIR\resources\multiace\post_process_virtual_toolheads.exe"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\resources\multiace\post_process_virtual_toolheads.exe"
    Delete "$INSTDIR\resources\multiace\Uninstall MultiACE Scripts.exe"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MultiACE Scripts"
SectionEnd
