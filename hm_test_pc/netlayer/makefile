CC = gcc
OBJECTS = netlayer.o mr_common.o  

netlayer: $(OBJECTS)
	$(CC) -o  netlayer $(OBJECTS) -lpthread

netlayer.o: netlayer.c netlayer.h
	$(CC) -g -c netlayer.c

mr_commom.o: mr_common.c mr_common.h
	$(CC) -g -c mr_common.c



clean :
	rm $(OBJECTS) 
