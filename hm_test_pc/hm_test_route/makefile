CC = gcc
OBJECTS = hm_test.o ../mr_common.o

hmtest: $(OBJECTS)
	$(CC) -o  hmtest $(OBJECTS) -lpthread

hm_test.o: hm_test.c hm_test.h
	$(CC) -g -c hm_test.c

mr_commom.o: ../mr_common.c ../mr_common.h
	$(CC) -g -c ../mr_common.c

clean :
	rm $(OBJECTS) 
