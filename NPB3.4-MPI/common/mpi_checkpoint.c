#include "mpi_checkpoint.h"

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

static char checkpoint_prefix[4096] = "checkpoint";
/* minimum checkpoint interval in seconds */
static int checkpoint_min_interval = 0;
static int initialized = 0;
static int verbose = 0;
static int no_checkpoint = 0;
/* the name of the checkpoint from which we plan to restore the program */
static const char* checkpoint_filename = 0;

static void read_configuration_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == 0) {
        fprintf(stderr, "unable to open \"%s\": %s\n", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }
    size_t n = 4096;
    char* line = (char*)malloc(n);
    ssize_t line_length = 0;
    while ((line_length = getline(&line, &n, file)) != -1) {
        char* first1 = line;
        char* last1 = strchr(line, '=');
        if (last1 == 0) { continue; }
        char* first2 = last1+1;
        while (first1 != last1 && isspace(*first1)) { ++first1; }
        while (first1 != last1 && isspace(*(last1-1))) { --last1; }
        *last1 = 0;
        char* last2 = line + line_length;
        while (first2 != last2 && isspace(*first2)) { ++first2; }
        while (first2 != last2 && isspace(*(last2-1))) { --last2; }
        *last2 = 0;
        if (strcmp(first1, "checkpoint-prefix") == 0) {
            fflush(stderr);
            strcpy(checkpoint_prefix, first2);
        } else if (strcmp(first1, "checkpoint-min-interval") == 0) {
            checkpoint_min_interval = atoi(first2);
            if (checkpoint_min_interval < 0) {
                fprintf(stderr, "negative checkpoint interval: %d\n", checkpoint_min_interval);
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(first1, "verbose") == 0) {
            verbose = atoi(first2);
        } else {
            fprintf(stderr, "unknown configuartion option: %s\n", first1);
            exit(EXIT_FAILURE);
        }
    }
    free(line);
    if (fclose(file) == -1) { perror("fclose"); exit(EXIT_FAILURE); }
}

static void MPI_Checkpoint_init() {
    const char* config = getenv("MPI_CHECKPOINT_CONFIG");
    if (config != 0) {
        read_configuration_file(config);
        if (getenv("MPI_NO_CHECKPOINT") != 0) { no_checkpoint = 1; }
        const char* checkpoint_filename = getenv("MPI_CHECKPOINT");
        initialized = 1;
    }
}

int MPI_Checkpoint_create(MPI_Comm comm, MPI_File* file) {
    if (!initialized) { MPI_Checkpoint_init(); }
    /* return if no checkpoint is requested */
    if (no_checkpoint) { return MPI_ERR_NO_CHECKPOINT; }
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    /* create checkpoint using DMTCP */
    if (checkpoint_filename && strcmp(checkpoint_filename, "dmtcp") == 0) {
        MPI_Barrier(comm);
        if (rank == 0) {
            if (verbose) {
                fprintf(stderr, "rank %d creating checkpoint using DMTCP\n", rank);
                fflush(stderr);
            }
            system("dmtcp_command --bccheckpoint");
        }
        MPI_Barrier(comm);
        return MPI_ERR_NO_CHECKPOINT;
    }
    /* create checkpoint manually */
    char newfilename[4096] = {0};
    if (rank == 0) {
        size_t n = strlen(checkpoint_prefix);
        strncpy(newfilename, checkpoint_prefix, n);
        time_t now = time(0);
        struct tm t = {0};
        /* N.B. some MPI implementations disallow colon symbol in file names.
           Here we use underscore instead. */
        strftime(newfilename+n, sizeof(newfilename)-n,
                 "_%Y-%m-%dT%H_%M_%S%z.checkpoint", localtime_r(&now, &t));
        newfilename[sizeof(newfilename)-1] = 0;
    }
    MPI_Bcast(newfilename, sizeof(newfilename), MPI_CHAR, 0, comm);
    int ret = MPI_File_open(comm, newfilename, MPI_MODE_CREATE|MPI_MODE_WRONLY,
                            MPI_INFO_NULL, file);
    if (ret == MPI_SUCCESS) {
        if (verbose) {
            fprintf(stderr, "rank %d creating %s\n", rank, newfilename);
            fflush(stderr);
        }
    }
    return ret;
}

int MPI_Checkpoint_restore(MPI_Comm comm, MPI_File* file) {
    if (!initialized) { MPI_Checkpoint_init(); }
    const char* filename = checkpoint_filename;
    if (filename == 0) { filename = ""; }
    /* do nothing if DMTCP checkpoints are used */
    if (strcmp(filename, "dmtcp") == 0) { return MPI_ERR_NO_CHECKPOINT; }
    /* restore manually */
    int ret = MPI_File_open(comm, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, file);
    if (ret == MPI_SUCCESS && verbose) {
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

/* Fortran bindings */

void mpi_checkpoint_create_(MPI_Fint* comm, MPI_Fint* file, MPI_Fint* error) {
    MPI_File c_file = MPI_File_f2c(*file);
    *error = MPI_Checkpoint_create(MPI_Comm_f2c(*comm), &c_file);
    *file = MPI_File_c2f(c_file);
}

void mpi_checkpoint_restore_(MPI_Fint* comm, MPI_Fint* file, MPI_Fint* error) {
    MPI_File c_file = MPI_File_f2c(*file);
    *error = MPI_Checkpoint_restore(MPI_Comm_f2c(*comm), &c_file);
}

void mpi_checkpoint_close_(MPI_Fint* file, MPI_Fint* error) {
    MPI_File c_file = MPI_File_f2c(*file);
    *error = MPI_File_close(&c_file);
    *file = MPI_File_c2f(c_file);
}
