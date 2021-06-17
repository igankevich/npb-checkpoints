#!/bin/sh

set -e

benchmark() {
    name=$1
    shift
    nprocs=$1
    shift
    make -C .. $name NPROCS=$nprocs CLASS=A
    rm -f checkpoint.dat
    mpirun --oversubscribe -np $nprocs ../bin/$name.A.x "$@" 2>&1 | tee /tmp/orig.log
    mpirun --oversubscribe -np $nprocs ../bin/$name.A.x "$@" 2>&1 | tee /tmp/new.log
}

benchmark_v2() {
    name=$1
    shift
    class=$1
    shift
    nprocs=$1
    shift
    checkpoint=$1
    shift
    make -C .. $name NPROCS=$nprocs CLASS=$class
    export MPI_CHECKPOINT=$checkpoint 
    if test "$checkpoint" = "dmtcp"
    then
        rm -f --one-file-system dmtcp*sh *.dmtcp
        pkill -9 -f dmtcp || true
        dmtcp_coordinator --exit-on-last --daemon
        dmtcp_launch --join-coordinator --coord-host surge mpirun -np $nprocs ../bin/$name.$class.x "$@"
        ./dmtcp_restart_script.sh
    else
        rm -f checkpoint.dat *.checkpoint
        output=$(mktemp)
        id=$(cat /proc/sys/kernel/random/uuid)
        timestamp_start=$(date '+%s.%N')
        echo "id,name,class,nprocs,timestamp,t" >> $output
        echo -n "$id,$name,$class,$nprocs,$timestamp_start," >> $output
        time -f '%e' mpirun -np $nprocs ../bin/$name.$class.x "$@" 2>> $output
        checkpoints=$(ls *.checkpoint | sort)
        for i in $checkpoints
        do
            echo -n "$id,$name,$class,$nprocs,$(stat --format='%.W' $i)," >> $output
            export MPI_CHECKPOINT=$i
            time -f '%e' mpirun -np $nprocs ../bin/$name.$class.x "$@" 2>> $output
        done
        column -t -s, $output
        mkdir -p build
        mv $output build/$timestamp_start.$name.$class.$nprocs.csv
    fi
}

#benchmark ep 2
#benchmark bt 4
#benchmark_v2 cg 2 dmtcp
for nprocs in 8
do
    benchmark_v2 cg B $nprocs mpi
done
#benchmark dt 21 WH
#benchmark ft 2
#benchmark is 2
#benchmark lu 2
#benchmark mg 2
