#!/bin/bash

action=$1
images=$2
count=${3:-1}

[ -z "$1" -o -z "$2" ] && {
    printf "Usage: $(basename $0) COMMAND NAME [COUNT]\n"
    printf "\tCOMMAND:\n\n"
    printf "\t\trun  \t run the container test\n"
    printf "\t\tclean\t clean the container and images\n"
    printf "\n"
    printf "\tNAME      the name of container image\n"
    printf "\tCOUNT     the count of container\n\n"
    exit 0
}

create_image (){
    docker images | grep $images >/dev/null 2>/dev/null
    [ $? -ne 0 ] && {
        docker build -t $images:latest .
    }
}

## Create containers
create_container (){
    for i in $(seq 1 $count);do
        echo "Create the $i container";
        echo "docker run --privileged=true -dit --rm $images /usr/bin/bash";
        docker run --privileged=true -dit --rm $images /usr/bin/bash;
    done
}

## Start test
test_container (){
    for id in `docker ps | sed '1 d' | awk '{print $1 }'`;do
        echo "docker exec --privileged=true -dit $id 'testcase.sh"
        docker exec --privileged=true -dit $id bash -c "cd vm-scalability/;./testcase.sh"
    done
}

do_test (){
    create_image $@  # create image by Dockerfile
    create_container $@
    test_container $@
}

do_clean (){
    ## clean contaienrs
    for container in `docker ps -a | grep "$images" | awk '{print $1}'`;do 
        echo "docker remove $container"
        docker kill $container >/dev/null 2>/dev/null;
        docker rm $container >/dev/null 2>/dev/null;
    done

    sleep 2 # just sleep 
    ## clean images
    docker images | grep $images >/dev/null 2>/dev/null
    [ $? -eq 0 ] && {
        echo "remove images"
        docker rmi $images
    }
}

main(){
    case "$1" in
    "run") do_test;;
    "clean") do_clean;;
    esac
}

main $action
