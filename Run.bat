mkdir obj
windres src\resources.rc -o obj\resources.o
IF ERRORLEVEL 1 (pause) ELSE (
	mkdir bin
	gcc src\main.c obj\resources.o -l gdi32 -mwindows -o bin\Snake.exe
	IF ERRORLEVEL 1 (pause) ELSE (start bin\Snake.exe))