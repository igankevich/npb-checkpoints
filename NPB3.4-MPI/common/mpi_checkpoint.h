#include <mpi.h>

enum {
    MPI_ERR_NO_CHECKPOINT=999,
};

int MPI_Checkpoint_create(MPI_Comm comm, MPI_File* file);
int MPI_Checkpoint_restore(MPI_Comm comm, MPI_File* file);
int MPI_Checkpoint_close(MPI_File* file);
