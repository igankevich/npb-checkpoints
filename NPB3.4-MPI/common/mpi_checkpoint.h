#define _GNU_SOURCE
#include <mpi.h>

enum {
    MPI_ERR_NO_CHECKPOINT=999,
};

int MPI_Checkpoint_create(MPI_Comm comm, MPI_File* file);
int MPI_Checkpoint_restore(MPI_Comm comm, MPI_File* file);
int MPI_Checkpoint_close(MPI_File* file);
int MPI_Checkpoint_init();
int MPI_Checkpoint_finalize();
int MPI_Checkpoint_write(MPI_File fh, const void *buf, int count, MPI_Datatype datatype);
int MPI_Checkpoint_write_ordered(MPI_File fh, const void *buf, int count, MPI_Datatype datatype);
int MPI_Checkpoint_read(MPI_File fh, void *buf, int count, MPI_Datatype datatype);
int MPI_Checkpoint_read_ordered(MPI_File fh, void *buf, int count, MPI_Datatype datatype);
