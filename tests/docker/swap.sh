#!/bin/bash

swapsize=$((10*1024))   # Mbytes
swapfile=/container-swap

swapon (){
    dd if=/dev/zero of=$swapfile bs=1M count=$swapsize 2>/dev/null >/dev/null
    mkswap $swapfile
}
