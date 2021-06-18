#include "mpi_checkpoint.h"
#include "time.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

int MPI_Checkpoint_create(MPI_Comm comm, const char* filename, MPI_File* file) {
    /* return if no checkpoint is requested */
    if (getenv("MPI_NO_CHECKPOINT") != 0) { return MPI_ERR_OTHER; }
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    /* create checkpoint using DMTCP */
    const char* checkpoint_filename = getenv("MPI_CHECKPOINT");
    if (checkpoint_filename && strcmp(checkpoint_filename, "dmtcp") == 0) {
        MPI_Barrier(comm);
        if (rank == 0) {
            fprintf(stderr, "rank %d creating checkpoint using DMTCP\n", rank);
            fflush(stderr);
            system("dmtcp_command --bccheckpoint");
        }
        MPI_Barrier(comm);
        return MPI_ERR_OTHER;
    }
    /* create checkpoint manually */
    char newfilename[4096] = {0};
    if (rank == 0) {
        size_t n = strlen(filename);
        strncpy(newfilename, filename, n);
        time_t now = time(0);
        struct tm t = {0};
        strftime(newfilename+n, sizeof(newfilename)-n,
                 "_%Y-%m-%dT%H_%M_%S.checkpoint", localtime_r(&now, &t));
        newfilename[sizeof(newfilename)-1] = 0;
    }
    MPI_Bcast(newfilename, sizeof(newfilename), MPI_CHAR, 0, comm);
    int ret = MPI_File_open(comm, filename, MPI_MODE_CREATE|MPI_MODE_WRONLY,
                            MPI_INFO_NULL, file);
    if (ret == MPI_SUCCESS) {
        fprintf(stderr, "rank %d creating %s\n", rank, newfilename);
        fflush(stderr);
    }
    return ret;
}

int MPI_Checkpoint_restore(MPI_Comm comm, MPI_File* file) {
    const char* filename = getenv("MPI_CHECKPOINT");
    if (filename == 0) { filename = ""; }
    /* do nothing if DMTCP checkpoints are used */
    if (strcmp(filename, "dmtcp") == 0) { return MPI_ERR_OTHER; }
    /* restore manually */
    return MPI_Checkpoint_restore_filename(comm, filename, file);
}

int MPI_Checkpoint_restore_filename(MPI_Comm comm, const char* filename, MPI_File* file) {
    int ret = MPI_File_open(comm, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, file);
    if (ret == MPI_SUCCESS) {
        int rank = 0;
        MPI_Comm_rank(comm, &rank);
        fprintf(stderr, "rank %d restored from %s\n", rank, filename);
        fflush(stderr);
    }
    return ret;
}

int MPI_Checkpoint_close(MPI_File* file) {
    return MPI_File_close(file);
}
