#!/bin/sh


m_addr=144

echo "open nodes: 1 to 9..."

counter=1
while [ $counter -lt 10 ]
do
	./maodv $counter &
	counter=`expr $counter + 1`
done

sleep 3

echo node 1 to 8 join m_addr $m_addr...

./control 1 join $m_addr
sleep 2

counter2=2
while [ $counter2 -lt 9 ]
do
	./control $counter2 join $m_addr
	counter2=`expr $counter2 + 1`
done



