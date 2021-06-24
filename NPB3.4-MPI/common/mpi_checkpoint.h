#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <mpi.h>

#define MPI_CHECKPOINT_NULL ((MPI_Checkpoint)0)

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MPI_ERR_NO_CHECKPOINT=999,
};

typedef struct mpi_checkpoint* MPI_Checkpoint;

int MPI_Checkpoint_create(MPI_Comm comm, MPI_Checkpoint* checkpoint);
int MPI_Checkpoint_restore(MPI_Comm comm, MPI_Checkpoint* checkpoint);
int MPI_Checkpoint_close(MPI_Checkpoint* checkpoint);
int MPI_Checkpoint_init();
int MPI_Checkpoint_finalize();
int MPI_Checkpoint_write(MPI_Checkpoint checkpoint, const void *buf, int count, MPI_Datatype datatype);
int MPI_Checkpoint_read(MPI_Checkpoint checkpoint, void *buf, int count, MPI_Datatype datatype);
MPI_Checkpoint MPI_Checkpoint_f2c(MPI_Fint f_checkpoint);
MPI_Fint MPI_Checkpoint_c2f(MPI_Checkpoint c_checkpoint);

#ifdef __cplusplus
}
#endif
