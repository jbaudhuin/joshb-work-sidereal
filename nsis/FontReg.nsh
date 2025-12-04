; FontReg.nsh
; Register fonts with Windows
; Based on NSIS Font.nsh examples

!ifndef FONTREG_NSH
!define FONTREG_NSH

!include "FontName.nsh"

!define CSIDL_FONTS 0x14
!ifndef HWND_BROADCAST
  !define HWND_BROADCAST 0xffff
!endif
!ifndef WM_FONTCHANGE
  !define WM_FONTCHANGE 0x001D
!endif

; Macro to install a TrueType Font
!macro InstallTTFFont FontFile
  Push "${FontFile}"
  Call InstallTTFFont
!macroend

Function InstallTTFFont
  Exch $0 ; Font file path
  Push $1
  Push $2
  Push $3
  
  ; Get the font file name without path
  StrCpy $1 $0 "" -12
  StrCmp $1 ".ttf" 0 +2
    StrCpy $1 $0 -4
  
  ; Get just the filename
  Push $0
  Call GetFileName
  Pop $2
  
  ; Copy font file to Windows Fonts directory
  ClearErrors
  CopyFiles /SILENT $0 "$FONTS"
  IfErrors 0 +2
    DetailPrint "Failed to copy font: $2"
  
  ; Get font name from the file
  Push $0
  Call GetFontName
  Pop $3
  
  ; Register the font in the registry
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts" "$3 (TrueType)" "$2"
  
  ; Notify Windows of font change
  SendMessage ${HWND_BROADCAST} ${WM_FONTCHANGE} 0 0 /TIMEOUT=5000
  
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; Helper function to extract filename from full path
Function GetFileName
  Exch $0
  Push $1
  Push $2
  
  StrCpy $2 $0 1 -1
  StrCmp $2 "\" 0 +3
    StrCpy $0 $0 -1
    Goto -3
  
  StrCpy $1 0
  loop:
    IntOp $1 $1 - 1
    StrCpy $2 $0 1 $1
    StrCmp $2 "" exit
    StrCmp $2 "\" 0 loop
    IntOp $1 $1 + 1
    StrCpy $0 $0 "" $1
  exit:
  
  Pop $2
  Pop $1
  Exch $0
FunctionEnd

!endif ; FONTREG_NSH
