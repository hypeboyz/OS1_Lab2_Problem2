#!/bin/sh
module="fifo"
device=${module}

rmmod -f ${module}

rm /dev/${device}*
