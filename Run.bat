windres resources.rc -o resources.o
IF ERRORLEVEL 1 (pause) ELSE (
	gcc main.c resources.o -l gdi32 -mwindows -oSnake.exe
	IF ERRORLEVEL 1 (pause) ELSE (start Snake.exe))