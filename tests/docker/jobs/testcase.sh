#!/bin/bash

export ROOT_PATH="$(pwd)"
export PATH=$PATH:$ROOT_PATH/bin

thp_action (){
    local action="never"

    [ $1 -gt 0 ] && {
        action="always"
    }

    if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then
        echo $action > /sys/kernel/mm/transparent_hugepage/enabled
    fi
    if [ -f /sys/kernel/mm/transparent_hugepage/defrag ]; then
        echo $action> /sys/kernel/mm/transparent_hugepage/defrag
    fi

    [ $action -eq 1 ] && { # configure the hugepages
        echo 10 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        #echo 1 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
    }
}

ksm_action (){
    local action=0

    [ $1 -gt 0 ] && {
        action=1
    }

    if [ -f /sys/kernel/mm/ksm/run ]; then
        echo $action > /sys/kernel/mm/ksm/run 2>/dev/null
    fi

    if [ $action -eq 1 ];then # configure the ksmd
        echo 1000 > /sys/kernel/mm/ksm/pages_to_scan
        echo 20 > /sys/kernel/mm/ksm/sleep_millisecs
        echo 0 > /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
    fi
}

memory_compact(){
    echo "memory_compact"

    # trigger compacting 
    if [ -f /proc/sys/vm/compact_memory ];then
        echo 1 > /proc/sys/vm/compact_memory
    fi
}

config_reclaim(){
    local total_mem

    total_mem=$(sed -n '/MemTotal/p' /proc/meminfo | awk '{print $2}')
    min_kbytes=$((total_mem/3))
    echo "min_kbytes is $min_kbytes"
    echo $min_kbytes > /proc/sys/vm/min_free_kbytes
    echo 7 > /proc/sys/vm/zone_reclaim_mode # fast reclaim
    echo 0 > /proc/sys/vm/laptop_mode # direct reclaim
    echo 100 > /proc/sys/vm/swappiness # prefer (file) 0 - 200 (anon)
}

main() {
    local random
    local containers=${1:-1}

    echo "Enable thp modules"
    thp_action 1     # action > 0, turn on the thp
    echo "Enable ksm modules"
    ksm_action 1     # action > 0, turn on the ksm

    config_reclaim
    cd $ROOT_PATH/modules
    for file in $(ls);do
        echo "Execute the script: $(basename $file)"
        ./$file start $containers
    done

    while true;do
        random=$(head -200 /dev/urandom | cksum | cut -f1 -d' ')
        [ $((random%800)) -eq 1 ] && {
            memory_compact
        }
        sleep 10
    done
}

do_signal(){
    thp_action 0
    ksm_action 0

    cd $ROOT_PATH/modules
    for file in $(ls);do
        echo "stop the script: $(basename $file)"
        ./$file stop
    done

    exit
}

trap do_signal INT
trap do_signal QUIT
main $@
