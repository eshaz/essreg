@ECHO OFF
REM ESSCHECK.BAT: the checks of esssb16 and of the chip in one run, with
REM the games after them. essprobe plays a game's DMA 5 on the chip as
REM esssb16 does, before esssb16 is in, HDPMI32i and esssb16 load,
REM sb16test checks esssb16 with the game's 16-bit DMA as BLASTER has it
REM and on 5, and the games play with it on 5, which esssb16 switches
REM without a restart: Pinball World, Pinball Dreams, whose driver unmasks
REM its DMA after the command, and Absolute Pinball, a protected-mode game,
REM through HDPMI32i. Everything goes to RESULTS, to send back with the
REM recording of the probe's sounds, if one was made. After each game,
REM esssb16 run again logs the buffer that it last carried over from the
REM game's 16-bit DMA, if any.
REM
REM Run it at the DOS prompt, without Windows, from the folder of the
REM programs, C:\ESS unless ESSDIR names another, all on drive C:
REM   ESSCHECK [Pinball World's folder] [Pinball Dreams' folder]
REM            [Absolute Pinball's folder]
REM Without them, the usual folders are looked in.
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
SET L=%R%\ESSCHECK.TXT
SET PW=%1
SET PD=%2
SET AP=%3
IF "%PW%"=="" IF EXIST C:\21STCENT\PWORLD\SETSND.EXE SET PW=C:\21STCENT\PWORLD
IF "%PW%"=="" IF EXIST C:\PWORLD\SETSND.EXE SET PW=C:\PWORLD
IF "%PW%"=="" IF EXIST C:\GAMES\PWORLD\SETSND.EXE SET PW=C:\GAMES\PWORLD
IF "%PD%"=="" IF EXIST C:\21STCENT\DREAMS\PD.EXE SET PD=C:\21STCENT\DREAMS
IF "%PD%"=="" IF EXIST C:\DREAMS\PD.EXE SET PD=C:\DREAMS
IF "%PD%"=="" IF EXIST C:\GAMES\DREAMS\PD.EXE SET PD=C:\GAMES\DREAMS
IF "%AP%"=="" IF EXIST C:\21STCENT\ABSOLUTE\ABS.EXE SET AP=C:\21STCENT\ABSOLUTE
IF "%AP%"=="" IF EXIST C:\ABSOLUTE\ABS.EXE SET AP=C:\ABSOLUTE
IF "%AP%"=="" IF EXIST C:\GAMES\ABSOLUTE\ABS.EXE SET AP=C:\GAMES\ABSOLUTE

ECHO esscheck 6: Pinball World in %PW%, Pinball Dreams in %PD% >> %L%
ECHO esscheck 6: Absolute Pinball in %AP% >> %L%
ECHO esscheck: the checks of esssb16 and the chip, saved in %R%
VER >> %L%
SET > %R%\SET.TXT
IF EXIST C:\CONFIG.SYS TYPE C:\CONFIG.SYS > %R%\CONFIG.TXT
IF EXIST C:\AUTOEXEC.BAT TYPE C:\AUTOEXEC.BAT > %R%\AUTOEXEC.TXT
IF EXIST C:\WINDOWS\DOSSTART.BAT TYPE C:\WINDOWS\DOSSTART.BAT > %R%\DOSSTART.TXT
MEM /C > %R%\MEM1.TXT
MEM /D > %R%\MEMD1.TXT

ECHO.
ECHO 1. essprobe: a game's DMA 5 on the chip as esssb16 plays it, before
ECHO    esssb16 is in
IF NOT EXIST %R%\STREAM.TXT GOTO PROBE
ECHO    Its report is there from a run before.
CHOICE /C:SR /N Press S to go on to esssb16 or R to run it again
IF ERRORLEVEL 2 GOTO PROBE
GOTO TSRLOAD
:PROBE
ECHO    Its sounds, about half a minute, can be recorded: start the
ECHO    recorder, then press Enter at the level tone.
CHOICE /C:RS /N Press R to record them or S to play them unrecorded
IF ERRORLEVEL 2 GOTO PRBQUIET
%ESSDIR%\ESSPROBE /R /N /C:stream /O:%R%\STREAM.TXT
GOTO PRBEND
:PRBQUIET
%ESSDIR%\ESSPROBE /N /C:stream /O:%R%\STREAM.TXT
:PRBEND
IF ERRORLEVEL 5 GOTO PRBDONE
IF ERRORLEVEL 4 GOTO TSRIN
IF ERRORLEVEL 2 GOTO STOPPED
:PRBDONE
ECHO essprobe: STREAM.TXT >> %L%

:TSRLOAD
ECHO.
ECHO 2. HDPMI32i and esssb16
IF EXIST %ESSDIR%\HDPMI32I.EXE LH %ESSDIR%\HDPMI32I.EXE -r -x > %R%\HDPMI.TXT
%ESSDIR%\ESSSB16 > %R%\ESSSB16.TXT
GOTO TSRCHK
:TSRIN
ECHO    esssb16 was in already (AUTOEXEC.BAT?), which essprobe can't have:
ECHO    restart without it for that part.
ECHO essprobe: skipped, esssb16 in already >> %L%
%ESSDIR%\ESSSB16 > %R%\ESSSB16.TXT
:TSRCHK
SET E=0
IF ERRORLEVEL 1 SET E=1
IF ERRORLEVEL 5 SET E=0
TYPE %R%\ESSSB16.TXT
TYPE %R%\ESSSB16.TXT >> %L%
MEM /C > %R%\MEM2.TXT
MEM /D > %R%\MEMD2.TXT
IF "%E%"=="1" GOTO STOPPED

ECHO.
ECHO 3. sb16test: the automatic checks, about two minutes, then again with
ECHO    the game's 16-bit DMA on 5
%ESSDIR%\SB16TEST /A /Q /O:%R%\SB16TEST.TXT
ECHO sb16test: SB16TEST.TXT >> %L%
%ESSDIR%\ESSSB16 /H5 >> %L%
%ESSDIR%\SB16TEST /A /Q /H:5 /O:%R%\SB16H5.TXT
ECHO sb16test on DMA 5: SB16H5.TXT >> %L%

ECHO.
ECHO 4. The games, with the game's 16-bit DMA on 5. After each, Y or N for
ECHO    how it sounded.
%ESSDIR%\ESSSB16 /H5 >> %L%
IF NOT "%PW%"=="" GOTO PWSTART
ECHO    Pinball World isn't in its usual folders: ESSCHECK folder, to play it.
GOTO DREAMS
:PWSTART
ECHO.
ECHO    Pinball World's SETSND: Setup Soundcard, then Sound Blaster 16 with
ECHO    DMA 5, which SETSND may not find by itself, then Test Sound; listen
ECHO    to the tune, then quit.
PAUSE
CD %PW%
SETSND
CD %ESSDIR%
%ESSDIR%\ESSSB16 >> %L%
SET Q=SETSND tune with DMA 5
SET RET=PWGAME
GOTO ASK
:PWGAME
ECHO.
ECHO    Pinball World: play a ball, listen, then quit.
PAUSE
CD %PW%
IF EXIST RUN.BAT CALL RUN.BAT
IF NOT EXIST RUN.BAT PWORLD
CD %ESSDIR%
%ESSDIR%\ESSSB16 >> %L%
SET Q=Pinball World with DMA 5
SET RET=DREAMS
GOTO ASK

:DREAMS
IF "%PD%"=="" GOTO ABSOLUTE
IF EXIST %PD%\SOUND.CFG GOTO PDGAME
ECHO.
ECHO    Pinball Dreams has no SOUND.CFG, and its SETSOUND stops the machine
ECHO    under EMM386 with this much memory: Pinball Dreams left out.
ECHO Pinball Dreams: no SOUND.CFG >> %L%
GOTO ABSOLUTE
:PDGAME
ECHO.
ECHO    Pinball Dreams, with the Sound Blaster 16 of its SOUND.CFG, whose
ECHO    driver unmasks the DMA after its command: play a ball, listen, then
ECHO    quit.
PAUSE
CD %PD%
PD
CD %ESSDIR%
%ESSDIR%\ESSSB16 >> %L%
SET Q=Pinball Dreams with DMA 5
SET RET=ABSOLUTE
GOTO ASK

:ABSOLUTE
IF "%AP%"=="" GOTO END
ECHO.
ECHO    Absolute Pinball's SETUP: Sound Blaster 16 at 220, IRQ 5 and DMA 5,
ECHO    then its test sound; listen, then save and quit. It needs the
ECHO    HDPMI32I.EXE of essreg's third_party\hdpmi32i, which has a fix: with
ECHO    the one of HX's releases, the machine stops at the first sound.
PAUSE
CD %AP%
SETUP
CD %ESSDIR%
%ESSDIR%\ESSSB16 >> %L%
SET Q=Absolute Pinball's test sound with DMA 5
SET RET=APGAME
GOTO ASK
:APGAME
ECHO.
ECHO    Absolute Pinball: play a ball, listen, then quit.
PAUSE
CD %AP%
ABS
CD %ESSDIR%
%ESSDIR%\ESSSB16 >> %L%
SET Q=Absolute Pinball with DMA 5
SET RET=END
GOTO ASK

:ASK
CHOICE /C:YN %Q%: did it sound right
IF ERRORLEVEL 2 ECHO %Q%: no >> %L%
IF NOT ERRORLEVEL 2 ECHO %Q%: yes >> %L%
GOTO %RET%

:STOPPED
ECHO stopped: see the lines above >> %L%
ECHO.
ECHO Stopped: the lines above say why.
GOTO QUIT
:END
ECHO done >> %L%
ECHO.
ECHO Done. Send the folder %R% back, and the recording if you made one.
:QUIT
