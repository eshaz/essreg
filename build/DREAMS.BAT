@ECHO OFF
REM DREAMS.BAT: Pinball Dreams' SETSOUND with less and less of esssb16's
REM setup around it, to find what stops the machine. Run it at the DOS
REM prompt right after a restart, from the folder of the programs, C:\ESS
REM unless ESSDIR names another:
REM   DREAMS [Pinball Dreams' folder]
REM The first run plays SETSOUND with nothing loaded, then with HDPMI32i;
REM the next runs, each after a restart, with esssb16 alone, then with
REM HDPMI32i and esssb16 as ESSCHECK loads them. RESULTS\DREAMS.TXT keeps
REM where it got to: a setup that started and never came back is the one
REM that stopped the machine.
REM
REM (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
REM
REM Licensed under GPL Version 3.0

IF "%ESSDIR%"=="" SET ESSDIR=C:\ESS
IF EXIST %ESSDIR%\ESSSB16.COM GOTO HAVEPROG
ECHO The programs aren't in %ESSDIR%: SET ESSDIR=their folder, then again.
GOTO QUIT
:HAVEPROG
C:
CD %ESSDIR%
SET R=%ESSDIR%\RESULTS
IF NOT EXIST %R%\NUL MD %R%
SET L=%R%\DREAMS.TXT
SET PD=%1
IF "%PD%"=="" IF EXIST C:\21STCENT\DREAMS\SETSOUND.EXE SET PD=C:\21STCENT\DREAMS
IF "%PD%"=="" IF EXIST C:\DREAMS\SETSOUND.EXE SET PD=C:\DREAMS
IF "%PD%"=="" IF EXIST C:\GAMES\DREAMS\SETSOUND.EXE SET PD=C:\GAMES\DREAMS
IF NOT "%PD%"=="" GOTO HAVEGAME
ECHO Pinball Dreams isn't in its usual folders: DREAMS folder, to play it.
GOTO QUIT
:HAVEGAME
IF NOT EXIST %L% ECHO dreams 1: SETSOUND in %PD% >> %L%
FIND "setup 1," %L% > NUL
IF ERRORLEVEL 1 GOTO NOTHING
FIND "setup 2," %L% > NUL
IF ERRORLEVEL 1 GOTO HDPMI
FIND "setup 3," %L% > NUL
IF ERRORLEVEL 1 GOTO TSRONLY
FIND "setup 4," %L% > NUL
IF ERRORLEVEL 1 GOTO BOTH
ECHO All four setups were tried:
TYPE %L%
GOTO DONE

:NOTHING
SET S=1
SET W=nothing loaded
SET RET=HDPMI
GOTO PLAY
:HDPMI
SET S=2
SET W=HDPMI32i
IF EXIST %ESSDIR%\HDPMI32I.EXE LH %ESSDIR%\HDPMI32I.EXE -r -x >> %L%
SET RET=RESTART
GOTO PLAY
:TSRONLY
SET S=3
SET W=esssb16 alone
%ESSDIR%\ESSSB16 >> %L%
SET RET=RESTART
GOTO PLAY
:BOTH
SET S=4
SET W=HDPMI32i and esssb16
IF EXIST %ESSDIR%\HDPMI32I.EXE LH %ESSDIR%\HDPMI32I.EXE -r -x >> %L%
%ESSDIR%\ESSSB16 >> %L%
SET RET=DONE
GOTO PLAY

:PLAY
ECHO setup %S%, %W%: started >> %L%
ECHO.
ECHO %S%. SETSOUND with %W%: choose the Sound Blaster 16 and its test, as
ECHO    before. If the machine stops, restart it and run DREAMS again.
PAUSE
MEM /C | FIND "SMARTDRV" > NUL
IF NOT ERRORLEVEL 1 SMARTDRV /C
CD %PD%
SETSOUND
CD %ESSDIR%
CHOICE /C:YN Did its test sound right
IF ERRORLEVEL 2 ECHO setup %S%, %W%: back, it sounded wrong >> %L%
IF NOT ERRORLEVEL 2 ECHO setup %S%, %W%: back, it sounded right >> %L%
GOTO %RET%

:RESTART
ECHO.
ECHO Restart the machine, then run DREAMS again for the next setup.
GOTO QUIT
:DONE
ECHO.
ECHO Done: send %R%\DREAMS.TXT back.
:QUIT
