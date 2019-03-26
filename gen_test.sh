#!/bin/sh
if [ -z "$1" ]
then
    exit
fi

./mem_test > /dev/null
min=$(cat out | wc -l)
mv out min

number=$1
for x in $(seq $1)
do
    ./mem_test > /dev/null
    lines=$(cat out | wc -l)
    if [ $lines -lt $min -a $lines -gt 0 ]
    then
        mv out min
        min=$lines
        echo min=$min $(date)
    fi
done
