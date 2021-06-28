This repository contains the implementation of _application-level_ checkpoint
and restart for NAS parallel benchmarks. This implementation was tested and is
known to pass verification tests when the application is restarted from the
checkpoint. Check the git log for all the modifications and programming effort
that was needed to reach the goal.

Implementation: `NPB3.4-MPI/common/mpi_checkpoint.c`

Benchmark script: `NPB3.4-MPI/benchmark.sh`
