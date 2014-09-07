#!/bin/sh 

kver="$1"
root="/lib/modules/$kver/kernel/drivers/hid/"

for i in hid-logitech-dj.ko hid-logitech-dj.ko.xz; do
	[ -f "$root/__no__${i}__no__" ] && \
		mv "$root/__no__${i}__no__" "$root/${i}" 
done
true
