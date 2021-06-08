#include "mpi_checkpoint.h"

int MPI_Checkpoint_create(MPI_Comm comm, const char* filename, MPI_File* file) {
    return MPI_File_open(comm, filename, MPI_MODE_CREATE|MPI_MODE_WRONLY,
                         MPI_INFO_NULL, file);
}

int MPI_Checkpoint_restore(MPI_Comm comm, const char* filename, MPI_File* file) {
    return MPI_File_open(comm, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, file);
}

int MPI_Checkpoint_close(MPI_File* file) {
    return MPI_File_close(file);
}
