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
    make ../common/mpi_checkpoint.o
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
        timestamp_start=$(rm -f .timestamp; touch .timestamp; stat --format='%.Y' .timestamp)
        echo "id,name,class,nprocs,timestamp,t" >> $output
        echo -n "$id,$name,$class,$nprocs,$timestamp_start," >> $output
        if test "$checkpoint" = "no"
        then
            export MPI_NO_CHECKPOINT=1
        fi
        time -f '%e' mpiexec -n $nprocs ../bin/$name.$class.x "$@" 2>> $output
        checkpoints=$(find . -name '*.checkpoint' | sort)
        for i in $checkpoints
        do
            echo -n "$id,$name,$class,$nprocs,$(stat --format='%.Y' $i)," >> $output
            export MPI_CHECKPOINT=$i
            time -f '%e' mpiexec -n $nprocs ../bin/$name.$class.x "$@" 2>> $output
        done
        column -t -s, $output
        output_dir=../output/$checkpoint-checkpoints
        mkdir -p $output_dir
        mv -vn $output ../output/$checkpoint-checkpoints/$timestamp_start.$name.$class.$nprocs.csv
    fi
}

cd CG
manifest=$(realpath ../../manifest.scm)
eval $(guix environment --manifest=$manifest --search-paths)
nodes=$(scontrol show hostnames "$SLURM_JOB_NODELIST")
for i in $nodes
do
    ssh $i "guix environment --manifest=$manifest --search-paths" &
done
wait
#cd ~/mpi-hello-world
#env | grep SLURM | sort
#mpicc hello.c -o hello-mpich
#mpirun $PWD/hello-mpich

for i in $(expr $SLURM_JOB_NUM_NODES \* 16)
do
    for j in mpi #no
    do
        #j=mpi
        #benchmark_v2 cg C $i $j
        #benchmark_v2 ep C $i $j
        # square number of processes
        i=$(echo "import math
print(math.floor(math.sqrt($i))**2)" | python3)
        echo "new nprocs = $i"
        benchmark_v2 bt C $i $j
    done
done
#benchmark dt 21 WH
#benchmark ft 2
#benchmark is 2
#benchmark lu 2
#benchmark mg 2
#benchmark ep 2
#benchmark bt 4
#benchmark_v2 cg 2 dmtcp
