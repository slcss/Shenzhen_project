CC = arm-linux-gcc-4.4.3
OBJECTS = hm_main.o ../mr_common.o hm_timer.o hm_queue_manage.o hm_slot.o hm_with_lowmac.o

highmac: $(OBJECTS)
	$(CC) -o  highmacp64_cs $(OBJECTS) -lpthread

hm_main.o: hm_main.c hm_common.h
	$(CC) -g -c hm_main.c

mr_commom.o: ../mr_common.c ../mr_common.h
	$(CC) -g -c ../mr_common.c

hm_timer.o: hm_timer.c hm_timer.h
	$(CC) -g -c hm_timer.c

hm_queue_manage.o: hm_queue_manage.c hm_queue_manage.h
	$(CC) -g -c hm_queue_manage.c

hm_slot.o: hm_slot.c hm_slot.h
	$(CC) -g -c hm_slot.c

hm_with_lowmac.o: hm_with_lowmac.c hm_with_lowmac.h
	$(CC) -g -c hm_with_lowmac.c

clean :
	rm $(OBJECTS) 
