#!/bin/sh
module="fifo"
device="fifo"

rmmod ${module}

rm /dev/${device}[0-3]

