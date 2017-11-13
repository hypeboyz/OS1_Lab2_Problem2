#!/bin/sh
module="fifo"
device=${module}
mode="664"
dev_nr=1

while getopts "d::" o;
do
    case "${o}" in
	d)
	    dev_nr=${OPTARG}
	    ;;
	*)
	    echo "Usage: $0 [-S|-ST|-L] [-l <number of loops>] [-t <number of threads>]";
	    exit -1
	    ;;
    esac
done

insmod ./${module}.ko fifo_nr_devs=${dev_nr}

major=$(gawk '/'${module}'/ { print $1 }' /proc/devices)

for i in `seq 0 $(expr ${dev_nr} - 1)`
do
    mknod -m $mode /dev/${device}$(expr 2 \* $i) c $major $(expr 2 \* $i);
    mknod -m $mode /dev/${device}$(expr 2 \* $i + 1) c $major $(expr 2 \* $i + 1);
done
