#!/bin/sh

dir="`dirname $0`"

for i in `find . '(' -name "*.c" -o -name "*.h" ')' -a '(' '!' -path './wpj_sysepoll/epoll-lib/*' -a '!' -path './intl/*' ')'`; do
	if [ $1 == "cosmetics" ]; then
		vim -u NONE -s "$dir/cosmetics.vim" $i
	elif [ $1 == "indent" ]; then
		indent -kr -i8 $i
	fi
done

stty sane
