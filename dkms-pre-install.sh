#!/bin/sh 

kver="$1"

root="/lib/modules/$kver/kernel/drivers/hid/"

[ -f "/etc/modprobe.d/hid-logitech-dj.conf" ] ||
	touch /etc/modprobe.d/hid-logitech-dj.conf

for i in hid-logitech-dj.ko hid-logitech-dj.ko.xz; do
	[ -e "$root/${i}" ] && mv "$root/${i}" "$root/__no__${i}__no__" 
done
true

