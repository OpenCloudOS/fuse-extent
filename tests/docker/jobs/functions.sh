#!/bin/sh

script="$1"
action="$2"
shift 2

start (){
    return 0
}

stop (){
    return 0
}

restart (){
    stop $@
    start $@
}

reload() {
    restart $@
}

. $script

case "$action" in 
"start")    start $@;;
"stop")     stop $@;;
"restart")  restart $@;;
esac
