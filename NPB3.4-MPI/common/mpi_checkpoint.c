#include "mpi_checkpoint.h"

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO
#include "miniz.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char checkpoint_prefix[4096] = "checkpoint";
/* minimum checkpoint interval in seconds */
static int checkpoint_min_interval = 0;
static int last_checkpoint_timestamp = 0;
static int initialized = 0;
static int verbose = 0;
static int no_checkpoint = 0;
static int compression_level = 0;
/* the name of the checkpoint from which we plan to restore the program */
static const char* checkpoint_filename = 0;
static mz_stream compressor = {0};
static mz_stream decompressor = {0};
static char* compression_buffer = 0;

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
            char* suffix = last2;
            while (first2 != suffix && !isdigit(*(suffix-1))) { --suffix; }
            int multiplier = 1;
            if (strcmp(suffix, "s") == 0) { multiplier = 1; }
            else if (strcmp(suffix, "m") == 0) { multiplier = 60; }
            else if (strcmp(suffix, "h") == 0) { multiplier = 60*60; }
            else if (strcmp(suffix, "d") == 0) { multiplier = 24*60*60; }
            else if (*suffix != 0) {
                fprintf(stderr, "unknown interval suffix: %s\n", suffix);
                exit(EXIT_FAILURE);
            }
            *suffix = 0;
            checkpoint_min_interval = atoi(first2);
            checkpoint_min_interval *= multiplier;
            if (checkpoint_min_interval < 0) {
                fprintf(stderr, "bad checkpoint interval: %d\n", checkpoint_min_interval);
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(first1, "verbose") == 0) {
            verbose = atoi(first2);
        } else if (strcmp(first1, "compression-level") == 0) {
            compression_level = atoi(first2);
            if (compression_level < MZ_NO_COMPRESSION ||
                compression_level > MZ_UBER_COMPRESSION) {
                fprintf(stderr, "bad compression level: %d\n", compression_level);
                exit(EXIT_FAILURE);
            }
        } else {
        }
    }
    free(line);
    if (fclose(file) == -1) { perror("fclose"); exit(EXIT_FAILURE); }
}

int MPI_Checkpoint_init() {
    strcpy(checkpoint_prefix, program_invocation_short_name);
    const char* config = getenv("MPI_CHECKPOINT_CONFIG");
    if (config != 0) {
        read_configuration_file(config);
    }
    if (getenv("MPI_NO_CHECKPOINT") != 0) { no_checkpoint = 1; }
    checkpoint_filename = getenv("MPI_CHECKPOINT");
    memset(&compressor, 0, sizeof(compressor));
    memset(&decompressor, 0, sizeof(decompressor));
    int ret = mz_deflateInit(&compressor, compression_level);
    ret |= mz_inflateInit(&decompressor);
    initialized = 1;
    return ret == 0 ? MPI_SUCCESS : MPI_ERR_OTHER;
}

int MPI_Checkpoint_finalize() {
    int ret = mz_deflateEnd(&compressor);
    ret |= mz_inflateEnd(&decompressor);
    return ret == 0 ? MPI_SUCCESS : MPI_ERR_OTHER;
}

int MPI_Checkpoint_create(MPI_Comm comm, MPI_File* file) {
    if (!initialized) { MPI_Checkpoint_init(); }
    /* return if no checkpoint is requested */
    if (no_checkpoint) { return MPI_ERR_NO_CHECKPOINT; }
    int current_timestamp = time(0);
    /* return if the last checkpoint is recent enough */
    if (current_timestamp-last_checkpoint_timestamp < checkpoint_min_interval) {
        return MPI_ERR_NO_CHECKPOINT;
    }
    last_checkpoint_timestamp = current_timestamp;
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

int MPI_Checkpoint_write(MPI_File fh, const void *buf, int count,
                         MPI_Datatype datatype) {
    return MPI_File_write(fh,buf,count,datatype,MPI_STATUS_IGNORE);
}

int MPI_Checkpoint_write_ordered(MPI_File fh, const void *buf, int count,
                                 MPI_Datatype datatype) {
    if (compression_level != 0) {
        int element_size = 0;
        MPI_Type_size(datatype, &element_size);
        int size_in_bytes = count*element_size;
        compression_buffer = realloc(compression_buffer, size_in_bytes);
        if (compression_buffer == 0) { return MPI_ERR_OTHER; }
        compressor.next_in = buf;
        compressor.avail_in = size_in_bytes;
        compressor.next_out = compression_buffer;
        compressor.avail_out = size_in_bytes;
        //mz_deflateReset(&compressor);
        if (mz_deflateInit(&compressor, compression_level) != MZ_OK) {
            return MPI_ERR_OTHER;
        }
        if (mz_deflate(&compressor, MZ_FINISH) != MZ_STREAM_END) {
            return MPI_ERR_OTHER;
        }
        /*
        fprintf(stderr, "total in = %d, total out = %d, array size %d, count %d\n",
                compressor.total_in, compressor.total_out, size_in_bytes, count);
                */
        int n = compressor.total_out;
        int ret = MPI_File_write_ordered(fh,&n,1,MPI_INT,MPI_STATUS_IGNORE);
        return MPI_File_write_ordered(fh,compression_buffer,n,MPI_BYTE,MPI_STATUS_IGNORE);
    }
    return MPI_File_write_ordered(fh,buf,count,datatype,MPI_STATUS_IGNORE);
}

int MPI_Checkpoint_read(MPI_File fh, void *buf, int count,
                        MPI_Datatype datatype) {
    return MPI_File_read(fh,buf,count,datatype,MPI_STATUS_IGNORE);
}

int MPI_Checkpoint_read_ordered(MPI_File fh, void *buf, int count, MPI_Datatype datatype) {
    if (compression_level != 0) {
        int buffer_size = 0;
        int element_size = 0;
        MPI_Type_size(datatype, &element_size);
        int size_in_bytes = element_size*count;
        int ret = 0;
        ret = MPI_File_read_ordered(fh,&buffer_size,1,MPI_INT,MPI_STATUS_IGNORE);
        if (ret != MPI_SUCCESS) { return ret; }
        compression_buffer = realloc(compression_buffer, buffer_size);
        if (compression_buffer == 0) { return MPI_ERR_OTHER; }
        ret = MPI_File_read_ordered(fh,compression_buffer,buffer_size,MPI_BYTE,
                                    MPI_STATUS_IGNORE);
        if (ret != MPI_SUCCESS) { return ret; }
        decompressor.next_in = compression_buffer;
        decompressor.avail_in = buffer_size;
        decompressor.next_out = buf;
        decompressor.avail_out = size_in_bytes;
        if (mz_inflate(&decompressor, MZ_FINISH) != MZ_STREAM_END) { return MPI_ERR_OTHER; }
        /*
        fprintf(stderr, "decompressor total in = %d, total out = %d\n",
                decompressor.total_in, decompressor.total_out);
        */
        return ret;
    }
    return MPI_File_read_ordered(fh,buf,count,datatype,MPI_STATUS_IGNORE);
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

void mpi_checkpoint_init_(MPI_Fint* error) {
    *error = MPI_Checkpoint_init();
}

void mpi_checkpoint_finalize_(MPI_Fint* error) {
    *error = MPI_Checkpoint_finalize();
}

void mpi_checkpoint_write_(MPI_Fint* fh, char* buf, MPI_Fint* count,
                           MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_write(MPI_File_f2c(*fh), buf, *count,
                                  MPI_Type_f2c(*datatype));
}

void mpi_checkpoint_write_ordered_(MPI_Fint* fh, char* buf, MPI_Fint* count,
                                   MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_write_ordered(MPI_File_f2c(*fh), buf, *count,
                                          MPI_Type_f2c(*datatype));
}

void mpi_checkpoint_read_(MPI_Fint* fh, char* buf, MPI_Fint* count,
                          MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_read(MPI_File_f2c(*fh), buf, *count,
                                 MPI_Type_f2c(*datatype));
}

void mpi_checkpoint_read_ordered_(MPI_Fint* fh, char* buf, MPI_Fint* count,
                                  MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_read_ordered(MPI_File_f2c(*fh), buf, *count,
                                         MPI_Type_f2c(*datatype));
}
