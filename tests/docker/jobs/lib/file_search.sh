#!/bin/bash

# add pagecache

do_scan(){
    rootdirs=$(grep ext4 /proc/mounts | awk '{print $2}')
    for root in $rootdirs;do
        for file in $(find $root -type f);do
            dd if=$file of=/dev/null 2>/dev/null
        done
    done
}

main(){
    local sec
    local random

    while true;do
        do_scan
        random=$(head -200 /dev/urandom | cksum | cut -f1 -d' ')
        let sec=random%30
        sleep $sec
    done
}

main
