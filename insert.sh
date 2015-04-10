#!/bin/sh -x

dir=/home/ghigo/logitech-m560/hid-logitech-dj-stable

cd $i
su ghigo -c "cd $dir; make clean && make"

rmmod hid_logitech_hidpp
insmod $dir/hid-logitech-hidpp.ko

