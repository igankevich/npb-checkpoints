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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

enum checkpoint_flags { CHECKPOINT_READ_ONLY = 1, CHECKPOINT_WRITE_ONLY = 2 };

struct mpi_checkpoint {
    int fd;
    void* data;
    size_t size;
    size_t offset;
    /* the number of bytes that are "freed" (MADV_DONTNEED)*/
    size_t start;
    enum checkpoint_flags flags;
    MPI_Comm communicator;
};

static char checkpoint_prefix[4096] = "checkpoint";
/* minimum checkpoint interval in seconds */
static int checkpoint_min_interval = 0;
const size_t checkpoint_initial_size = 4096;
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
static double checkpoint_t0 = 0;
static double checkpoint_t1 = 0;
/* fortran checkpoints */
static MPI_Checkpoint checkpoints[4096/sizeof(MPI_Checkpoint)];
static int checkpoints_count = 0;
static size_t page_size = 4096;

static struct mpi_checkpoint* checkpoint_alloc() {
    struct mpi_checkpoint* checkpoint = malloc(sizeof(struct mpi_checkpoint));
    if (!checkpoint) {
        fprintf(stderr, "not enough memory\n");
        exit(EXIT_FAILURE);
    }
    memset(checkpoint, 0, sizeof(struct mpi_checkpoint));
    return checkpoint;
}

static void checkpoint_free(struct mpi_checkpoint* checkpoint) {
    if (checkpoint->data) {
        if (checkpoint->flags & CHECKPOINT_WRITE_ONLY) {
            if (msync(checkpoint->data, checkpoint->size, MS_SYNC) == -1) {
                perror("msync");
                exit(EXIT_FAILURE);
            }
        }
        if (munmap(checkpoint->data, checkpoint->size) == -1) {
            perror("munmap");
            exit(EXIT_FAILURE);
        }
        checkpoint->data = 0;
        checkpoint->size = 0;
    }
    if (checkpoint->fd != -1) {
        if (checkpoint->flags & CHECKPOINT_WRITE_ONLY) {
            if (ftruncate(checkpoint->fd, checkpoint->offset) == -1) {
                perror("ftruncate");
                exit(EXIT_FAILURE);
            }
        }
        checkpoint->offset = 0;
        if (close(checkpoint->fd) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
        checkpoint->fd = -1;
    }
    free(checkpoint);
}

static int add_fortran_checkpoint(MPI_Checkpoint c_checkpoint, MPI_Fint* error) {
    if (checkpoints_count == sizeof(checkpoints)/sizeof(MPI_Checkpoint)) {
        *error = MPI_ERR_OTHER;
        return -1;
    }
    checkpoints[checkpoints_count] = c_checkpoint;
    return checkpoints_count++;
}

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

/* The path must end with "/". */
static int mkdir_p(char* path, mode_t mode) {
    char* first = path, last = 0;
    while (*path) {
        if (*path == '/') {
            *path = 0;
            if (mkdir(first, mode) == -1 && errno != EEXIST) {
                *path = '/';
                return -1;
            }
            *path = '/';
            first = path+1;
        }
        ++path;
    }
    return 0;
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
    page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size <= 0) { page_size = 4096UL; }
    return ret == 0 ? MPI_SUCCESS : MPI_ERR_OTHER;
}

int MPI_Checkpoint_finalize() {
    int ret = mz_deflateEnd(&compressor);
    ret |= mz_inflateEnd(&decompressor);
    return ret == 0 ? MPI_SUCCESS : MPI_ERR_OTHER;
}

int MPI_Checkpoint_create(MPI_Comm comm, MPI_Checkpoint* file) {
    checkpoint_t0 = MPI_Wtime();
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
            double t0 = MPI_Wtime();
            system("dmtcp_command --bccheckpoint");
            double t1 = MPI_Wtime();
            if (verbose) {
                fprintf(stderr, "rank %d checkpoint took %f seconds\n", rank, t1-t0);
                fflush(stderr);
            }
        }
        MPI_Barrier(comm);
        return MPI_ERR_NO_CHECKPOINT;
    }
    /* create checkpoint manually */
    char newfilename[4096];
    /* synchronize time */
    time_t now = time(0);
    MPI_Bcast(&now, sizeof(now), MPI_BYTE, 0, comm);
    if (snprintf(newfilename, sizeof(newfilename), "%s.%lu.checkpoint/",
                 checkpoint_prefix, now) < 0) {
        perror("snprintf");
        exit(EXIT_FAILURE);
    }
    if (mkdir_p(newfilename, 0755) == -1) {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }
    if (snprintf(newfilename, sizeof(newfilename), "%s.%lu.checkpoint/%d",
                 checkpoint_prefix, now, rank) < 0) {
        perror("snprintf");
        exit(EXIT_FAILURE);
    }
    MPI_Checkpoint checkpoint = checkpoint_alloc();
    checkpoint->fd = open(newfilename, O_CREAT|O_RDWR|O_CLOEXEC, 0644);
    if (checkpoint->fd == -1) {
        fprintf(stderr, "Unable to open checkpoint \"%s\" for writing: %s\n",
                newfilename, strerror(errno));
        exit(EXIT_FAILURE);
    }
    checkpoint->flags = CHECKPOINT_WRITE_ONLY;
    checkpoint->size = checkpoint_initial_size;
    if (ftruncate(checkpoint->fd, checkpoint->size) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    checkpoint->data = mmap(0, checkpoint->size, PROT_WRITE, MAP_SHARED, checkpoint->fd, 0);
    if (checkpoint->data == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    checkpoint->communicator = comm;
    *file = checkpoint;
    if (verbose) {
        fprintf(stderr, "rank %d creating %s\n", rank, newfilename);
        fflush(stderr);
    }
    return MPI_SUCCESS;
}

int MPI_Checkpoint_restore(MPI_Comm comm, MPI_Checkpoint* file) {
    checkpoint_t0 = MPI_Wtime();
    if (!initialized) { MPI_Checkpoint_init(); }
    /* return if no checkpoint is requested */
    if (no_checkpoint) { return MPI_ERR_NO_CHECKPOINT; }
    const char* filename = checkpoint_filename;
    if (filename == 0) { return MPI_ERR_NO_CHECKPOINT; }
    if (strcmp(filename, "") == 0) { return MPI_ERR_NO_CHECKPOINT; }
    /* do nothing if DMTCP checkpoints are used */
    if (strcmp(filename, "dmtcp") == 0) { return MPI_ERR_NO_CHECKPOINT; }
    /* restore manually */
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    char newfilename[4096];
    if (snprintf(newfilename, sizeof(newfilename), "%s/%d", filename, rank) < 0) {
        perror("snprintf");
        exit(EXIT_FAILURE);
    }
    int checkpoint_fd = open(newfilename, O_RDONLY|O_CLOEXEC);
    if (checkpoint_fd == -1) {
        fprintf(stderr, "Unable to open checkpoint \"%s\" for reading: %s\n",
                newfilename, strerror(errno));
        exit(EXIT_FAILURE);
    }
    MPI_Checkpoint checkpoint = checkpoint_alloc();
    checkpoint->fd = checkpoint_fd;
    checkpoint->flags = CHECKPOINT_READ_ONLY;
    struct stat status;
    if (fstat(checkpoint->fd, &status) == -1) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }
    checkpoint->size = status.st_size;
    if (checkpoint->size == 0) {
        checkpoint->data = 0;
    } else {
        checkpoint->data = mmap(0, checkpoint->size, PROT_READ, MAP_PRIVATE, checkpoint->fd, 0);
        if (checkpoint->data == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }
        if (madvise(checkpoint->data, checkpoint->size, MADV_SEQUENTIAL) == -1) {
            perror("madvise");
            exit(EXIT_FAILURE);
        }
    }
    if (verbose) {
        fprintf(stderr, "rank %d restored from %s\n", rank, newfilename);
        fflush(stderr);
    }
    checkpoint->communicator = comm;
    *file = checkpoint;
    return MPI_SUCCESS;
}

int MPI_Checkpoint_close(MPI_Checkpoint* checkpoint) {
    MPI_Comm comm = (*checkpoint)->communicator;
    checkpoint_free(*checkpoint);
    *checkpoint = MPI_CHECKPOINT_NULL;
    checkpoint_t1 = MPI_Wtime();
    if (verbose) {
        int rank = 0;
        MPI_Comm_rank(comm, &rank);
        fprintf(stderr, "rank %d checkpoint create/restore took %f seconds\n",
                rank, checkpoint_t1-checkpoint_t0);
        fflush(stderr);
    }
    return MPI_SUCCESS;
}

int MPI_Checkpoint_write(MPI_Checkpoint checkpoint, const void *buf, int count, MPI_Datatype datatype) {
    int element_size = 0;
    MPI_Type_size(datatype, &element_size);
    int size_in_bytes = count*element_size;
    size_t old_size = 0;
    while (checkpoint->size - checkpoint->offset < size_in_bytes) {
        size_t new_size = checkpoint->offset + size_in_bytes;
        size_t remainder = new_size%page_size;
        if (remainder != 0) { new_size += page_size-remainder; }
        if (ftruncate(checkpoint->fd, new_size) == -1) {
            perror("ftruncate");
            exit(EXIT_FAILURE);
        }
        void* new_data = mremap(checkpoint->data, checkpoint->size, new_size, MREMAP_MAYMOVE);
        if (!new_data) {
            perror("mremap");
            exit(EXIT_FAILURE);
        }
        old_size = checkpoint->size;
        checkpoint->data = new_data;
        checkpoint->size = new_size;
    }
    memcpy(((char*)checkpoint->data) + checkpoint->offset, buf, size_in_bytes);
    if (old_size != 0) {
        if (madvise(((char*)checkpoint->data) + checkpoint->start,
                    old_size-checkpoint->start, MADV_DONTNEED) == -1) {
            perror("madvise");
            exit(EXIT_FAILURE);
        }
        checkpoint->start = old_size;
    }
    checkpoint->offset += size_in_bytes;
    return MPI_SUCCESS;
}

/*
int MPI_Checkpoint_write_ordered(MPI_Checkpoint fh, const void *buf, int count,
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
        int n = compressor.total_out;
        int ret = MPI_File_write_ordered(fh,&n,1,MPI_INT,MPI_STATUS_IGNORE);
        return MPI_File_write_ordered(fh,compression_buffer,n,MPI_BYTE,MPI_STATUS_IGNORE);
    }
    return MPI_File_write_ordered(fh,buf,count,datatype,MPI_STATUS_IGNORE);
}
*/

int MPI_Checkpoint_read(MPI_Checkpoint checkpoint, void *buf, int count, MPI_Datatype datatype) {
    int element_size = 0;
    MPI_Type_size(datatype, &element_size);
    int size_in_bytes = count*element_size;
    if (checkpoint->offset + size_in_bytes > checkpoint->size) {
        return MPI_ERR_OTHER;
    }
    memcpy(buf, ((char*)checkpoint->data) + checkpoint->offset, size_in_bytes);
    checkpoint->offset += size_in_bytes;
    size_t num_pages = (checkpoint->offset-checkpoint->start) / page_size;
    if (num_pages != 0) {
        if (madvise(((char*)checkpoint->data) + checkpoint->start,
                    checkpoint->offset-checkpoint->start, MADV_DONTNEED) == -1) {
            perror("madvise");
            exit(EXIT_FAILURE);
        }
        checkpoint->start += num_pages*page_size;
    }
    return MPI_SUCCESS;
}

/*
int MPI_Checkpoint_read_ordered(MPI_Checkpoint fh, void *buf, int count, MPI_Datatype datatype) {
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
        return ret;
    }
    return MPI_File_read_ordered(fh,buf,count,datatype,MPI_STATUS_IGNORE);
}
*/

/* Fortran bindings */

MPI_Checkpoint MPI_Checkpoint_f2c(MPI_Fint f_checkpoint) {
    if (f_checkpoint < 0 || f_checkpoint >= checkpoints_count) {
        return MPI_CHECKPOINT_NULL;
    }
    return checkpoints[f_checkpoint];
}

MPI_Fint MPI_Checkpoint_c2f(MPI_Checkpoint c_checkpoint) {
    for (int i=0; i<sizeof(checkpoints)/sizeof(MPI_Checkpoint); ++i) {
        if (checkpoints[i] == c_checkpoint) {
            return i;
        }
    }
    return -1;
}

void mpi_checkpoint_create_(MPI_Fint* comm, MPI_Fint* f_checkpoint, MPI_Fint* error) {
    MPI_Checkpoint c_checkpoint = MPI_CHECKPOINT_NULL;
    *error = MPI_Checkpoint_create(MPI_Comm_f2c(*comm), &c_checkpoint);
    if (*error != MPI_SUCCESS) { return; }
    *f_checkpoint = add_fortran_checkpoint(c_checkpoint, error);
}

void mpi_checkpoint_restore_(MPI_Fint* comm, MPI_Fint* f_checkpoint, MPI_Fint* error) {
    MPI_Checkpoint c_checkpoint = MPI_CHECKPOINT_NULL;
    *error = MPI_Checkpoint_restore(MPI_Comm_f2c(*comm), &c_checkpoint);
    *f_checkpoint = add_fortran_checkpoint(c_checkpoint, error);
}

void mpi_checkpoint_close_(MPI_Fint* f_checkpoint, MPI_Fint* error) {
    MPI_Checkpoint c_checkpoint = MPI_Checkpoint_f2c(*f_checkpoint);
    if (c_checkpoint == MPI_CHECKPOINT_NULL) { *error = MPI_ERR_OTHER; return; }
    *error = MPI_Checkpoint_close(&c_checkpoint);
    *f_checkpoint = -1;
}

void mpi_checkpoint_init_(MPI_Fint* error) {
    *error = MPI_Checkpoint_init();
}

void mpi_checkpoint_finalize_(MPI_Fint* error) {
    *error = MPI_Checkpoint_finalize();
}

void mpi_checkpoint_write_(MPI_Fint* f_checkpoint, char* buf, MPI_Fint* count,
                           MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_write(MPI_Checkpoint_f2c(*f_checkpoint), buf, *count,
                                  MPI_Type_f2c(*datatype));
}

void mpi_checkpoint_read_(MPI_Fint* f_checkpoint, char* buf, MPI_Fint* count,
                          MPI_Fint* datatype, MPI_Fint* error) {
    *error = MPI_Checkpoint_read(MPI_Checkpoint_f2c(*f_checkpoint), buf, *count,
                                 MPI_Type_f2c(*datatype));
}

/*
#pragma weak MPI_CHECKPOINT_READ = mpi_checkpoint_read_
#pragma weak mpi_checkpoint_read = mpi_checkpoint_read_
#pragma weak mpi_checkpoint_read__ = mpi_checkpoint_read_
#pragma weak mpi_checkpoint_read_f = mpi_checkpoint_read_
#pragma weak mpi_checkpoint_read_f08 = mpi_checkpoint_read_
*/
