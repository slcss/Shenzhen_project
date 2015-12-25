#第一个目标会作为默认目标
all : maodv control nl_test
#声明默认目标all为伪目标
.PHONY : all


#CC = arm-linux-g++
CC = gcc
objects1 = maodv.o mr_commom.o list.o timer.o quit.o
objects2 = control.o mr_commom.o
objects3 = nl_test.o mr_common.o

maodv : $(objects1)
	$(CC) -g -o maodv $(objects1) -lpthread -lm
maodv.o : maodv.c
	$(CC) -c -g -o maodv.o maodv.c
mr_commom.o: mr_common.c mr_common.h
	$(CC) -c mr_common.c -g -o mr_commom.o
list.o : list.c list.h
	$(CC) -c list.c -g -o list.o
timer.o : timer.c timer.h
	$(CC) -c timer.c -g -o timer.o
quit.o : quit.c
	$(CC) -c quit.c -g -o quit.o


control : $(objects2)
	$(CC) -g -o control $(objects2) -lpthread
control.o  : control.c
	$(CC) -c -g -o control.o  control.c

nl_test : $(objects3)
	$(CC) -g -o nl_test $(objects3) -lpthread
nl_test.o : nl_test.c
	$(CC) -c -g -o nl_test.o  nl_test.c

clean:
	rm  *.o maodv control nl_test
