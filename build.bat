del *.obj
PATH C:\WINDOWS;C:\WINDOWS\COMMAND;C:\NETCOM;%PATH%;C:\WATCOM\BINNT;C:\WATCOM\BINW
SET INCLUDE=C:\WATCOM\H;C:\WATCOM\MFC\INCLUDE;C:\WATCOM\H\NT
SET WATCOM=C:\WATCOM
SET EDPATH=C:\WATCOM\EDDAT
wmake
copy essreg.exe build\essreg.exe /y
copy 1869opl3.com build\1869opl3.com /y
copy esfmpat.exe build\esfmpat.exe /y

