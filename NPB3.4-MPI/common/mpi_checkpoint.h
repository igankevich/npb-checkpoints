#include <mpi.h>

int MPI_Checkpoint_create(MPI_Comm comm, const char* filename, MPI_File* file);
int MPI_Checkpoint_restore(MPI_Comm comm, MPI_File* file);
int MPI_Checkpoint_restore_filename(MPI_Comm comm, const char* filename, MPI_File* file);
int MPI_Checkpoint_close(MPI_File* file);
