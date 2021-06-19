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
    make -C .. clean
    make ../common/mpi_checkpoint.o
    make -C .. $name NPROCS=$nprocs CLASS=$class
    export MPI_CHECKPOINT=$checkpoint 
    exe=../bin/$name.$class.x
    if test "$checkpoint" = "dmtcp"
    then
        rm -f --one-file-system dmtcp*sh *.dmtcp
        pkill -9 -f dmtcp || true
        dmtcp_coordinator --exit-on-last --daemon
        dmtcp_launch --join-coordinator --coord-host surge mpirun -np $nprocs $exe "$@"
        ./dmtcp_restart_script.sh
    else
        rm -f checkpoint.dat *.checkpoint
        output=$(mktemp -p $TMPDIR)
        id=$(cat /proc/sys/kernel/random/uuid)
        timestamp_start=$(rm -f .timestamp; touch .timestamp; stat --format='%.Y' .timestamp)
        echo "id,name,class,nprocs,timestamp,size,t" >> $output
        echo -n "$id,$name,$class,$nprocs,$timestamp_start,," >> $output
        if test "$checkpoint" = "no"
        then
            export MPI_NO_CHECKPOINT=1
        fi
        export MPI_CHECKPOINT_CONFIG=$(mktemp -p $TMPDIR)
        cat > $MPI_CHECKPOINT_CONFIG << EOF
checkpoint-prefix = $id
verbose = 1
EOF
        time --format='%e' --output=$output --append mpiexec -n $nprocs $exe "$@"
        checkpoints=$(find . -name "$id*.checkpoint" | sort)
        for i in $checkpoints
        do
            echo -n "$id,$name,$class,$nprocs,$(stat --format='%.Y' $i),$(stat --format='%s' $i)," >> $output
            export MPI_CHECKPOINT=$i
            time --format='%e' --output=$output --append mpiexec -n $nprocs $exe "$@"
        done
        column -t -s, $output
        output_dir=../output/$checkpoint-checkpoints
        mkdir -p $output_dir
        mv -vn $output $output_dir/$timestamp_start.$name.$class.$nprocs.csv
    fi
}

manifest=$(realpath ../manifest.scm)
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

export TMPDIR=$HOME/tmp
cd CG
for i in $(expr $SLURM_JOB_NUM_NODES \* 16)
do
    for j in mpi #no
    do
        #j=mpi
        #benchmark_v2 cg C $i $j
        #benchmark_v2 ep C $i $j
        # square number of processes
#        i=$(echo "import math
#print(math.floor(math.sqrt($i))**2)" | python3)
#        echo "new nprocs = $i"
        #benchmark_v2 bt C $i $j
        #benchmark_v2 ft C $i $j
        #benchmark_v2 dt C $i $j TODO
        #benchmark_v2 ep C $i $j
        #benchmark_v2 is C $i $j
        benchmark_v2 lu C $i $j
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
