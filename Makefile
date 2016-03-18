CC		= arm-angstrom-linux-gnueabi-gcc
CCA		= arm-angstrom-linux-gnueabi-ar




CFLAGS		= -g -Wall  -fstack-protector-all -Wstack-protector -rdynamic
SO	= libevent_poll.so
ARC	= libevent_poll.a


LD		= ld
OBJ		= event.o
OBJ2	= event_test.o 
LFLAGS   	=  
HEADER		= event_poll.h
TARGET 		= event_test

all:  $(ARC) $(SO) $(TARGET)

$(ARC):$(OBJ)
		$(CCA) rcs $(ARC) $(OBJ) 

$(SO): $(OBJ)
		$(CC) -shared -Wl,-soname,$(SO) -o $(SO) $(OBJ)   -lc -lrt
		cp $(SO) /work/users/$(USER)/$(SO)
		cp $(ARC) /work/users/$(USER)/$(ARC)

%.o: %.c
		$(CC) $(CFLAGS)  -c $<

$(TARGET): $(OBJ2) $(SO) $(ARC)
		$(CC) -o $(TARGET) $(OBJ2) $(ARC)  $(LFLAGS) -lrt
		
		
		
	
clean:
		rm -f *.o *.a* *.so *~ $(TARGET)

distclean:	clean
		rm -f *.a* *.so*
