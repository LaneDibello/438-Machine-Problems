#!/bin/bash

MPORT=`expr 7776 + 3 \* $1 - 1`
SPORT=`expr 7777 + 3 \* $1 - 1`
FPORT=`expr 7778 + 3 \* $1 - 1`

./server -cip localhost -cp 7777 -p $MPORT -id $1 -t master &
sleep 1
./server -cip localhost -cp 7777 -p $SPORT -id $1 -t slave &
sleep 1
./synchronizer -cip localhost -cp 7777 -p $FPORT -id $1 &