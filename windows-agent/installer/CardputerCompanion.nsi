Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetDatablockOptimize on

!include "MUI2.nsh"

!ifndef VERSION
  !error "VERSION is required"
!endif
!ifndef AGENT_EXE
  !error "AGENT_EXE is required"
!endif
!ifndef OUTPUT
  !error "OUTPUT is required"
!endif

Name "Cardputer Codex Companion"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\CardputerCodexCompanion"
InstallDirRegKey HKCU "Software\CardputerCodexCompanion" "InstallDir"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Cardputer Codex Companion"
VIAddVersionKey /LANG=1033 "CompanyName" "Cardputer Codex Companion"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright 2026"
VIAddVersionKey /LANG=1033 "FileDescription" "Windows Machine Agent Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\cardputer-agent.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "pair"
!define MUI_FINISHPAGE_RUN_TEXT "Pair this computer with Cardputer"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Cardputer Codex Companion" SEC_AGENT
  SetShellVarContext current
  SetOutPath "$INSTDIR"

  nsExec::ExecToLog 'powershell.exe -NoProfile -Command "Unregister-ScheduledTask -TaskName $\"Cardputer Codex Companion$\" -Confirm:$$false -ErrorAction SilentlyContinue"'

  File /oname=cardputer-agent.exe "${AGENT_EXE}"
  File "install_task.xml.in"
  File "register_task.ps1"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\Cardputer Codex Companion"
  CreateShortCut "$SMPROGRAMS\Cardputer Codex Companion\Pair Device.lnk" \
    "$INSTDIR\cardputer-agent.exe" "pair"
  CreateShortCut "$SMPROGRAMS\Cardputer Codex Companion\Status.lnk" \
    "$INSTDIR\cardputer-agent.exe" "status"
  CreateShortCut "$SMPROGRAMS\Cardputer Codex Companion\Doctor.lnk" \
    "$INSTDIR\cardputer-agent.exe" "doctor"
  CreateShortCut "$SMPROGRAMS\Cardputer Codex Companion\Uninstall.lnk" \
    "$INSTDIR\Uninstall.exe"

  nsExec::ExecToStack 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\register_task.ps1" -Template "$INSTDIR\install_task.xml.in" -Executable "$INSTDIR\cardputer-agent.exe"'
  Pop $0
  Pop $1
  StrCmp $0 "0" task_registered
    DetailPrint "Scheduled Task registration failed: exit $0"
    Abort
  task_registered:

  WriteRegStr HKCU "Software\CardputerCodexCompanion" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion" \
    "DisplayName" "Cardputer Codex Companion"
  WriteRegStr HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion" \
    "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegDWORD HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion" \
    "NoModify" 1
  WriteRegDWORD HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion" \
    "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  nsExec::ExecToLog 'powershell.exe -NoProfile -Command "Unregister-ScheduledTask -TaskName $\"Cardputer Codex Companion$\" -Confirm:$$false -ErrorAction SilentlyContinue"'

  Delete "$INSTDIR\cardputer-agent.exe"
  Delete "$INSTDIR\install_task.xml.in"
  Delete "$INSTDIR\register_task.ps1"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$LOCALAPPDATA\CardputerCodexCompanion"
  RMDir /r "$SMPROGRAMS\Cardputer Codex Companion"

  DeleteRegKey HKCU "Software\CardputerCodexCompanion"
  DeleteRegKey HKCU \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CardputerCodexCompanion"
SectionEnd
