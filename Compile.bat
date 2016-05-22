windres resources.rc -o resources.o
gcc main.c resources.o -l gdi32 -mwindows -o Snake.exe
echo %ERRORLEVEL%
pause