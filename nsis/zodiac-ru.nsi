!define PRODUCT 'Zodiac'
!define VERSION '0.9.7'

!include FontReg.nsh
!include FontName.nsh
!include WinMessages.nsh

Name ${Product}

OutFile "${Product}-${VERSION}-installer.exe"
Icon "${NSISDIR}\Contrib\Graphics\Icons\orange-install.ico"
InstallDir "$PROGRAMFILES\Zodiac"
InstallDirRegKey HKLM "Software\Zodiac" "Install_Dir"
RequestExecutionLevel admin


;--------------------------------
!include "MUI2.nsh"
!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP "left.bmp"
!define MUI_HEADERIMAGE_BITMAP "top.bmp"
;!insertmacro MUI_LANGUAGE "Russian"

!define MUI_WELCOMEPAGE_TITLE "��������� ��������� ${PRODUCT} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "���������� �������� ��� ����� ��� ����� ��������� ��������� ${PRODUCT} ${VERSION} �� ��� ���������."

# These indented statements modify settings for MUI_PAGE_FINISH
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN "$INSTDIR\zodiac.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_RUN_TEXT "��������� ����������"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license-ru.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "Russian"

;--------------------------------

; The stuff to install
Section "����� ���������" SecMain

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  File ..\bin\Qt*.dll
  File ..\bin\icu*.dll
  File ..\bin\lib*.dll
  File ..\bin\zodiac.exe
  File "launch-session.bat"
  
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
  
  SetOutPath "$INSTDIR\style"
  File ..\bin\style\*
  
  SetOutPath "$INSTDIR\swe"
  File ..\bin\swe\*
  
  SetOutPath "$INSTDIR\text\en"
  File ..\bin\text\en\*
  
  SetOutPath "$INSTDIR\text\ru"
  File ..\bin\text\ru\*
  
  SetOutPath "$INSTDIR\sampleCharts"
  File ..\bin\sampleCharts\*.dat
  
  ; Write the installation path into the registry
  WriteRegStr HKLM Software\Zodiac "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "DisplayName" "Zodiac"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
  
  ; Register .zos file association
  WriteRegStr HKCR ".zos" "" "ZodiacSession"
  WriteRegStr HKCR "ZodiacSession" "" "Zodiac Session File"
  WriteRegStr HKCR "ZodiacSession\DefaultIcon" "" "$INSTDIR\zodiac.exe,0"
  WriteRegStr HKCR "ZodiacSession\shell\open\command" "" '"$INSTDIR\launch-session.bat" "%1"'
  
SectionEnd

Section "������" SecFonts
	SetOutPath "$FONTS"
	StrCpy $FONT_DIR $FONTS
	!insertmacro InstallTTFFont 'fonts\Almagest.ttf'
	!insertmacro InstallTTFFont 'fonts\DejaVuSans.ttf'
	!insertmacro InstallTTFFont 'fonts\DejaVuSansCondensed.ttf'
	!insertmacro InstallTTFFont 'fonts\DejaVuSerif.ttf'
	;File fonts\*.ttf
SectionEnd

; Optional section (can be disabled by the user)
Section "������ � ���� ����" SecFolder
  CreateDirectory "$SMPROGRAMS\Zodiac"
  CreateShortCut "$SMPROGRAMS\Zodiac\�������.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\Zodiac\������.lnk" "$INSTDIR\zodiac.exe" "" "$INSTDIR\zodiac.exe" 0
SectionEnd

Section "����� �� ������� ����" SecIco
  SetOutPath "$DESKTOP"
  CreateShortCut "$DESKTOP\Zodiac.lnk" "$INSTDIR\zodiac.exe"
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  ; Remove directories used
  RMDir /r "$SMPROGRAMS\Zodiac"
  RMDir /r "$INSTDIR"
  
  Delete "$DESKTOP\Zodiac.lnk"
  
  ; Remove file association
  DeleteRegKey HKCR ".zos"
  DeleteRegKey HKCR "ZodiacSession"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Zodiac"
  DeleteRegKey HKLM SOFTWARE\Zodiac
SectionEnd

;--------------------------------
;Descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "����� ���������, ������������ ��� ���������."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFonts} "������, ����������� ��� ����������� ����������� ������ � �������."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFolder} "����� � �������� ��� ������� ��� �������� ���������."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecIco} "����� �� ������� ����."
  !insertmacro MUI_FUNCTION_DESCRIPTION_END