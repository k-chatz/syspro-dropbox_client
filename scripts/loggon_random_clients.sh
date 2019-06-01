#!/usr/bin/env bash

for i in `seq 50`; do port=$(( i * ( 10 ) + RANDOM ));
 ./client -d clientdir -p $port -w 5 -b 43 -sp 8888 -sip 192.168.1.2;
 sleep 1;
 done
