windres src\resources.rc -o obj\resources.o
gcc src\main.c obj\resources.o -l gdi32 -mwindows -o bin\Snake.exe
echo %ERRORLEVEL%
pause