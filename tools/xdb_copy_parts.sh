#!/bin/bash

i=0
userfile=xdb_copy.users
partlines=100

rm -f $userfile

while read user
do
	echo $user >> $userfile
	i=`expr $i + 1`
	if [ `expr $i % $partlines` -eq 0 ]; then
		./xdb_copy.py 2>/dev/null
		rm -f $userfile
	fi
done
