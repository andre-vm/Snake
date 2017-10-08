RM := rm -rf

C_SRCS := src\main.c 
OBJS := obj\main.o \
	    obj\resources.o
LIBS := -lgdi32
EXE := bin\Snake.exe
DIRS := obj bin

$(EXE): $(OBJS) $(DIRS)
	gcc -mwindows -o "$@" $(OBJS) $(LIBS)
	
$(DIRS):
	-mkdir $@
	
obj\resources.o: src\resources.rc src\resources.h $(DIRS)
	windres -o "$@" "$<"
	
obj\main.o: src\main.c src\resources.h $(DIRS)
	gcc -O3 -Wall -c -fmessage-length=0 -o "$@" "$<"
	
run: $(EXE)
	$(EXE)

clean:
	-$(RM) $(OBJS) $(EXE)

.PHONY: run clean
