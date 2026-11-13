; The Sarxina OBS Plugin Manager's installer for Windows. Built by the target sarxina-obs-plugin-manager-installer
; (see installer.cmake), which hands it:
;   VERSION  the manager's version, x.y.z
;   STAGE    a folder holding the released zip's files laid out the way OBS 32.2 loads a plugin from ProgramData:
;              bin\64bit\sarxina-obs-plugin-manager.dll
;              data\...
;   OUTFILE  where the installer goes
;
; It puts the manager in C:\ProgramData\obs-studio\plugins\sarxina-obs-plugin-manager\, the folder OBS 32.2 and newer
; loads plugins from (a portable OBS skips it; its users extract the zip instead). Before that it checks that OBS
; Studio is installed, that it is 32.2 or newer, and that it is closed, and it takes away a manager extracted into the
; OBS folder the old way, so that OBS never loads two.

Unicode True
ManifestDPIAware True
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define NAME "Sarxina OBS Plugin Manager"
!define ID "sarxina-obs-plugin-manager"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SarxinaOBSPluginManager"
!define REPO "https://github.com/Sarxina/sarxina-obs-plugins"
!define OBS_MIN_MAJOR 32
!define OBS_MIN_MINOR 2

Name "${NAME}"
OutFile "${OUTFILE}"
BrandingText "Sarxina"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "Sarxina"
VIAddVersionKey "LegalCopyright" "GPL-2.0"

Var ObsDir

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "${NAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the ${NAME} into OBS Studio.$\r$\n$\r$\nOnce it is in, you add and remove Sarxina's plugins from a dock inside OBS.$\r$\n$\r$\nClose OBS before you go on."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "The manager is installed"
!define MUI_FINISHPAGE_TEXT "Open OBS, go to the Docks menu at the top and tick ${NAME}.$\r$\n$\r$\nIn its dock, pick a plugin from the list, hit Install, then restart OBS."
!define MUI_FINISHPAGE_LINK "Sarxina's plugins on GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION "${REPO}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Waits until OBS is closed, or the streamer gives up. Used by the installer and the uninstaller.
!macro WAIT_FOR_OBS_CLOSED un
Function ${un}WaitForObsClosed
  ${Do}
    nsExec::ExecToStack '"$SYSDIR\cmd.exe" /c "$SYSDIR\tasklist.exe" /FI "IMAGENAME eq obs64.exe" /NH | "$SYSDIR\find.exe" /I "obs64.exe"'
    Pop $0
    Pop $1
    ${If} $0 != 0
      ${Break}
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OBS Studio is open. Close it, then click Retry." /SD IDCANCEL IDRETRY +2
    Abort
  ${Loop}
FunctionEnd
!macroend
!insertmacro WAIT_FOR_OBS_CLOSED ""
!insertmacro WAIT_FOR_OBS_CLOSED "un."

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "The ${NAME} needs 64-bit Windows." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  StrCpy $INSTDIR "$APPDATA\obs-studio\plugins\${ID}"

  ReadRegStr $ObsDir HKLM "SOFTWARE\OBS Studio" ""
  ${If} $ObsDir == ""
  ${OrIfNot} ${FileExists} "$ObsDir\bin\64bit\obs64.exe"
    MessageBox MB_OK|MB_ICONSTOP "OBS Studio isn't installed on this computer.$\r$\n$\r$\nInstall OBS Studio first. For a portable OBS, extract the manager's zip into the OBS folder instead (${REPO})." /SD IDOK
    Abort
  ${EndIf}

  GetDLLVersion "$ObsDir\bin\64bit\obs64.exe" $R0 $R1
  IntOp $R2 $R0 >> 16
  IntOp $R2 $R2 & 0xFFFF
  IntOp $R3 $R0 & 0xFFFF
  ${If} $R2 < ${OBS_MIN_MAJOR}
  ${OrIf} $R2 == ${OBS_MIN_MAJOR}
  ${AndIf} $R3 < ${OBS_MIN_MINOR}
    MessageBox MB_OK|MB_ICONSTOP "Your OBS Studio is version $R2.$R3. The ${NAME} needs ${OBS_MIN_MAJOR}.${OBS_MIN_MINOR} or newer.$\r$\n$\r$\nUpdate OBS (Help > Check For Updates), then run this again." /SD IDOK
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  Call WaitForObsClosed

  ; A manager extracted into the OBS folder (the old way, and still the way for a portable OBS) would load beside this
  ; one. Its settings and connections are in the streamer's AppData and stay as they are.
  Delete "$ObsDir\obs-plugins\64bit\${ID}.dll"
  Delete "$ObsDir\obs-plugins\64bit\${ID}.dll.old"
  Delete "$ObsDir\obs-plugins\64bit\${ID}.pdb"
  RMDir /r "$ObsDir\data\obs-plugins\${ID}"

  ; Files of an earlier manager in this folder go, including the ones its own updating left for the next start
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"

  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Sarxina"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${REPO}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$ObsDir\bin\64bit\obs64.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  ; No DisplayVersion: the manager updates itself from its dock, and Windows would go on showing this one
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "Uninstall"
  Call un.WaitForObsClosed
  ; Only the manager. Plugins it installed stay, and run on their own; its settings stay in AppData.
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
