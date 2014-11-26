#!/bin/sh
module="fifo"
device="fifo"
mode="664"

insmod ./${module}.ko

major=$(gawk '/'${module}'/ { print $1 }' /proc/devices)

mknod -m $mode /dev/${device}0 c $major 0
mknod -m $mode /dev/${device}1 c $major 1
mknod -m $mode /dev/${device}2 c $major 2
mknod -m $mode /dev/${device}3 c $major 3
