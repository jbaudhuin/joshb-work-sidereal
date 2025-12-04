; FontName.nsh
; Get font name from TrueType font file
; Simplified version for common fonts

!ifndef FONTNAME_NSH
!define FONTNAME_NSH

; Function to get a friendly font name from a font file
; This is a simplified version that handles the fonts we're using
Function GetFontName
  Exch $0 ; Font file path
  Push $1
  Push $2
  
  ; Get the filename without path
  Push $0
  Call GetFileName
  Pop $1
  
  ; Extract font name based on common patterns
  StrCpy $2 $1 8
  StrCmp $2 "Almagest" 0 +3
    StrCpy $0 "Almagest"
    Goto done
  
  StrCpy $2 $1 9
  StrCmp $2 "DejaVuSan" 0 +3
    StrCpy $0 "DejaVu Sans"
    Goto check_condensed
  
  StrCpy $2 $1 10
  StrCmp $2 "DejaVuSeri" 0 +3
    StrCpy $0 "DejaVu Serif"
    Goto done
  
  ; Default: use filename without extension as font name
  StrCpy $0 $1 -4
  Goto done
  
  check_condensed:
  ; Check if it's DejaVu Sans Condensed
  StrCpy $2 $1 19
  StrCmp $2 "DejaVuSansCondensed" 0 done
    StrCpy $0 "DejaVu Sans Condensed"
  
  done:
  Pop $2
  Pop $1
  Exch $0
FunctionEnd

!endif ; FONTNAME_NSH
