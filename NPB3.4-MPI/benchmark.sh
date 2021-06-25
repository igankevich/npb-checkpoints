#!/bin/sh

set -e

profile() {
    time --format='%e' --append --output $output "$@"
}

profile2() {
    time --format='%e' --append --output ${output}2 "$@"
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
    exe=../bin/$name.$class.x
    unset MPI_CHECKPOINT
    if test "$checkpoint" = "dmtcp"
    then
        export MPI_CHECKPOINT="dmtcp"
        rm -rf --one-file-system dmtcp*sh *.dmtcp
        output=$(mktemp)
        id=$(cat /proc/sys/kernel/random/uuid)
        timestamp_start=$(rm -f .timestamp; touch .timestamp; stat --format='%.Y' .timestamp)
        echo "id,name,class,nprocs,timestamp,size,t" >> $output
        echo -n "$id,$name,$class,$nprocs,$timestamp_start,," >> $output
        pkill -9 -f dmtcp || true
        dmtcp_coordinator --exit-on-last --daemon
        set +e
        profile dmtcp_launch --no-gzip --join-coordinator --coord-host $(hostname) mpiexec -n $nprocs -f ../hosts $exe "$@"
        if test $? != 0
        then
            return
        fi
        set -e
        echo RESTORE
        pkill -f dmtcp || true
        sleep 1
        dmtcp_coordinator --exit-on-last --daemon
        cat $output >> ${output}2
        echo -n "$id,$name,$class,$nprocs,$(stat --format='%.Y' *.dmtcp | awk 'BEGIN{t=0}{if ($1>0+t) t=$1} END {print t}'),$(stat --format='%s' *.dmtcp | awk '{s+=$1} END {print s}')," >> ${output}2
        sed -i -e 's/ibrun_path=.*/ibrun_path=garbage/' dmtcp_restart_script.sh
        sed -i -e 's:/usr/bin/::g' dmtcp_restart_script.sh
        set +e
        profile2 sh -c './dmtcp_restart_script.sh || true'
        set -e
        cat ${output}2 > $output
        rm ${output}2
        column -t -s, $output
        output_dir=../output/$checkpoint-checkpoints
        mkdir -p $output_dir
        mv -vn $output $output_dir/$timestamp_start.$name.$class.$nprocs.csv
    else
        rm -rf --one-file-system checkpoint.dat *.checkpoint
        output=$(mktemp)
        id=$(cat /proc/sys/kernel/random/uuid)
        timestamp_start=$(rm -f .timestamp; touch .timestamp; stat --format='%.Y' .timestamp)
        echo "id,name,class,nprocs,timestamp,size,t" >> $output
        echo -n "$id,$name,$class,$nprocs,$timestamp_start,," >> $output
        if test "$checkpoint" = "no"
        then
            export MPI_NO_CHECKPOINT=1
        fi
        export MPI_CHECKPOINT_CONFIG=config.tmp
        cat > $MPI_CHECKPOINT_CONFIG << EOF
checkpoint-interval = 0
verbose = 1
EOF
        set +e
        profile mpiexec -n $nprocs -f ../hosts $exe "$@"
        if test $? != 0
        then
            return
        fi
        set -e
        checkpoints=$(find . -name "$name*.checkpoint" | sort)
        for ii in $checkpoints
        do
            echo -n "$id,$name,$class,$nprocs,$(stat --format='%.Y' $ii),$(stat --format='%s' $ii/* | awk '{s+=$1} END {print s}')," >> $output
            export MPI_CHECKPOINT=$ii
            profile mpiexec -n $nprocs -f ../hosts $exe "$@"
        done
        column -t -s, $output
        output_dir=../output/$checkpoint-checkpoints
        mkdir -p $output_dir
        mv -vn $output $output_dir/$timestamp_start.$name.$class.$nprocs.csv
    fi
}

manifest=$(realpath ../manifest.scm)
nodes=$(scontrol show hostnames "$SLURM_JOB_NODELIST")
for i in $nodes
do
    ssh $i "guix environment --manifest=$manifest --search-paths" &
done
wait
eval $(guix environment --manifest=$manifest --search-paths)

#cd ~/mpi-hello-world
#env | grep SLURM | sort
#mpicc hello.c -o hello-mpich
#mpiexec $PWD/hello-mpich

export HYDRA_IFACE=enp6s0
export HYDRA_RMK=user
export UCX_NET_DEVICES=enp6s0

cd CG
for i in $(expr $SLURM_JOB_NUM_NODES \* 16)
do
    for j in dmtcp
    do
        #for k in cg ep ft is lu mg
        for k in cg
        do
            benchmark_v2 $k C $i $j
        done
        #benchmark_v2 dt C $i $j BH
        # square number of processes
        i=$(echo "import math
print(math.floor(math.sqrt($i))**2)" | python3)
        echo "new nprocs = $i"
        #benchmark_v2 bt C $i $j
    done
done
