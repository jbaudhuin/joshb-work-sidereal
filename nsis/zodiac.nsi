!define PRODUCT 'Zodiac'
!define VERSION '0.9.1'

!include WinMessages.nsh
!include FontReg.nsh
!include FontName.nsh

Name ${Product}

OutFile "${Product}-${VERSION}-installer.exe"
Icon "${NSISDIR}\Contrib\Graphics\Icons\orange-install.ico"
InstallDir "$PROGRAMFILES64\Zodiac"
InstallDirRegKey HKLM "Software\Zodiac" "Install_Dir"
RequestExecutionLevel admin


;--------------------------------
!include "MUI2.nsh"
!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP "left.bmp"
!define MUI_HEADERIMAGE_BITMAP "top.bmp"
;!insertmacro MUI_LANGUAGE "Russian"

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${PRODUCT} ${VERSION} setup wizard"
;!define MUI_WELCOMEPAGE_TEXT "���������� �������� ��� ����� ��� ����� ��������� ��������� ${PRODUCT} ${VERSION} �� ��� ���������."

# These indented statements modify settings for MUI_PAGE_FINISH
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN "$INSTDIR\zodiac.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_RUN_TEXT "Run application"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;--------------------------------

; The stuff to install
Section "Essential files" SecMain

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Preserve existing settings.ini if it exists
  IfFileExists "$INSTDIR\settings.ini" 0 +2
    Rename "$INSTDIR\settings.ini" "$INSTDIR\settings.ini.backup"
  
  ; Main executable (required)
  File ..\bin\zodiac.exe
  File "README_FOR_USERS.txt"
  File "license.txt"
  
  ; Qt DLLs (if present - use /nonfatal so installer still works without them)
  File /nonfatal ..\bin\Qt*.dll
  File /nonfatal ..\bin\icu*.dll
  File /nonfatal ..\bin\lib*.dll
  
  ; OpenSSL DLLs (required for HTTPS/TLS - Google Maps API, etc.)
  File /nonfatal ..\bin\libssl-3-x64.dll
  File /nonfatal ..\bin\libcrypto-3-x64.dll
  
  ; LLVM/MinGW runtime DLLs (required for llvm-mingw builds)
  File /nonfatal ..\bin\libc++.dll
  File /nonfatal ..\bin\libunwind.dll
  
  SetOutPath "$INSTDIR\astroprocessor"
  File ..\bin\astroprocessor\*.csv
  
  SetOutPath "$INSTDIR\fileeditor"
  File ..\bin\fileeditor\*
  
  SetOutPath "$INSTDIR\imageformats"
  File ..\bin\imageformats\*.dll

  SetOutPath "$INSTDIR\i18n"
  File ..\bin\i18n\*.qm
  
  SetOutPath "$INSTDIR\images\aspects"
  File ..\bin\images\aspects\*
  
  SetOutPath "$INSTDIR\images\planets"
  File ..\bin\images\planets\*
  
  
  SetOutPath "$INSTDIR\images\planets-mini"
  File ..\bin\images\planets-mini\*
  
  SetOutPath "$INSTDIR\images\signs"
  File ..\bin\images\signs\*
  
  SetOutPath "$INSTDIR\chart"
  File ..\bin\chart\*
  
  SetOutPath "$INSTDIR\plain"
  File ..\bin\plain\*
  
  SetOutPath "$INSTDIR\planets"
  File ..\bin\planets\*
  
  SetOutPath "$INSTDIR\details"
  File ..\bin\details\*
  
  SetOutPath "$INSTDIR\platforms"
  File ..\bin\platforms\*
  
  SetOutPath "$INSTDIR\styles"
  File /nonfatal ..\bin\styles\*
  
  SetOutPath "$INSTDIR\tls"
  File /nonfatal ..\bin\tls\*
  
  SetOutPath "$INSTDIR\style"
  File ..\bin\style\*
  
  SetOutPath "$INSTDIR\swe"
  File ..\bin\swe\*
  
  ; Note: settings.ini deliberately excluded - will be created by application on first run
  ; This avoids including personal user preferences in the installer
  
  ; Restore backed-up settings.ini if it exists
  IfFileExists "$INSTDIR\settings.ini.backup" 0 +2
    Rename "$INSTDIR\settings.ini.backup" "$INSTDIR\settings.ini"
  
  SetOutPath "$INSTDIR\text\en"
  File ..\bin\text\en\*
  
  SetOutPath "$INSTDIR\text\ru"
  File ..\bin\text\ru\*
    
  SetOutPath "$INSTDIR\sampleCharts"
  File ..\bin\sampleCharts\*.dat
  
  ; Write the installation path into the registry
  WriteRegStr HKLM Software\Zodiac "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "DisplayName" "Zodiac Sidereal"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "DisplayIcon" "$INSTDIR\zodiac.exe,0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "Publisher" "Turtle Crescent Graphics"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
  
  ; Try to grant write permissions to installation directory so settings.ini can be created
  ; Use icacls command (built into Windows) to grant Users group write access
  nsExec::ExecToLog 'icacls "$INSTDIR" /grant Users:(OI)(CI)M /T /Q'
  
SectionEnd

Section "Fonts" SecFonts
  SetOutPath "$FONTS"
  !insertmacro InstallTTFFont 'fonts\Almagest.ttf'
  !insertmacro InstallTTFFont 'fonts\DejaVuSans.ttf'
  !insertmacro InstallTTFFont 'fonts\DejaVuSansCondensed.ttf'
  !insertmacro InstallTTFFont 'fonts\DejaVuSerif.ttf'
SectionEnd

; Optional section - curated default settings (only for new installations)
Section /o "Author's curated settings" SecSettings
  SetOutPath "$INSTDIR"
  
  ; Only install if settings.ini doesn't already exist
  IfFileExists "$INSTDIR\settings.ini" 0 +3
    ; Settings exist - skip installation
    Goto settings_done
  
  ; Settings don't exist - install curated defaults
  File "default-settings.ini"
  Rename "$INSTDIR\default-settings.ini" "$INSTDIR\settings.ini"
  
  settings_done:
SectionEnd

; Optional section (can be disabled by the user)
Section "Start menu shortcut" SecFolder
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Zodiac"
  CreateShortCut "$SMPROGRAMS\Zodiac\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\Zodiac\Zodiac.lnk" "$INSTDIR\zodiac.exe" "" "$INSTDIR\zodiac.exe" 0 SW_SHOWNORMAL "" "Zodiac Sidereal - Astrological Software" "$INSTDIR"
SectionEnd

Section "Desktop shortcut" SecIco
  SetOutPath "$INSTDIR"
  CreateShortCut "$DESKTOP\Zodiac.lnk" "$INSTDIR\zodiac.exe" "" "$INSTDIR\zodiac.exe" 0 SW_SHOWNORMAL "" "Zodiac Sidereal - Astrological Software" "$INSTDIR"
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  ; Remove shortcuts
  RMDir /r "$SMPROGRAMS\Zodiac"
  Delete "$DESKTOP\Zodiac.lnk"
  
  ; Preserve user data by backing it up before removal
  ; Check if settings.ini exists and preserve it
  IfFileExists "$INSTDIR\settings.ini" 0 +3
    CopyFiles "$INSTDIR\settings.ini" "$TEMP\zodiac_settings_backup.ini"
    MessageBox MB_YESNO "Preserve your settings and user data?$\n$\nThis includes:$\n- settings.ini (API keys, preferences)$\n- sampleCharts\ directory (sample chart files)$\n$\nClick Yes to keep them, No to delete everything." IDYES preserve_data
  
  ; User chose to delete everything or no settings exist
  RMDir /r "$INSTDIR"
  Goto end_uninstall
  
  preserve_data:
    ; Remove everything except user data
    Delete "$INSTDIR\zodiac.exe"
    Delete "$INSTDIR\uninstall.exe"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\astroprocessor"
    RMDir /r "$INSTDIR\chart"
    RMDir /r "$INSTDIR\details"
    RMDir /r "$INSTDIR\fileeditor"
    RMDir /r "$INSTDIR\fonts"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\i18n"
    RMDir /r "$INSTDIR\iconengines"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\images"
    RMDir /r "$INSTDIR\plain"
    RMDir /r "$INSTDIR\planets"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\style"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\swe"
    RMDir /r "$INSTDIR\text"
    ; Note: sampleCharts\ directory is preserved if user chose to keep data
    ; Note: settings.ini is preserved
    MessageBox MB_OK "Application removed. Your settings and chart files have been preserved in:$\n$INSTDIR"
  
  end_uninstall:
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac"
  DeleteRegKey HKLM SOFTWARE\Zodiac
SectionEnd

;--------------------------------
; Functions

; Check at startup if settings exist and disable curated settings option
Function .onInit
  ; Check if settings.ini already exists in the installation directory
  IfFileExists "$INSTDIR\settings.ini" 0 settings_check_done
    ; Settings exist - disable the curated settings section (make it read-only/grayed out)
    SectionSetFlags ${SecSettings} ${SF_RO}
    SectionSetText ${SecSettings} "Author's curated settings (already configured)"
  settings_check_done:
FunctionEnd

;--------------------------------
;Descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Essential files for running application."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFonts} "Font files for correct demonstration of glyphs and symbols."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecSettings} "Curated layout and display settings by the author (for new installations only). Includes optimized preferences for aspect orbs, house systems, and interface layout. Existing settings will be preserved if already present."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFolder} "Folder contains application shortcuts in Start menu."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecIco} "Desktop shortcut."
  !insertmacro MUI_FUNCTION_DESCRIPTION_END