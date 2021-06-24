/*
MPI-CHECKPOINT — C library that implements user-level MPI checkpoints.
© 2021 Ivan Gankevich

This file is part of MPI-CHECKPOINT.

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#ifndef MPI_CHECKPOINT_H
#define MPI_CHECKPOINT_H

#include <mpi.h>

#define MPI_CHECKPOINT_NULL ((MPI_Checkpoint)0)

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MPI_ERR_NO_CHECKPOINT=999,
};

typedef struct mpi_checkpoint* MPI_Checkpoint;

/**
  \brief Initialize MPI checkpoint library.
  \details
  This function parses configuration file (if any), reads environment variables
  and creates compressor/decompressor buffers.
  \return On success \c MPI_SUCCESS is returned. On error \c MPI_ERR_OTHER is returned.
  \section environ Environment variables
  \arg \c MPI_CHECKPOINT_CONFIG --- a path to the configuration file.
  \arg \c MPI_NO_CHECKPOINT --- if this variable is set, checkpoints and restarts are disabled.
  \arg \c MPI_CHECKPOINT --- a path to the directory that contains checkpoint files that
  are used to restore the program.
  \section config Configuartion file
  This file contains options in a form of "key=value". Possible keys are listed below.
  \arg \c checkpoint-prefix --- a path that is prepended to the checkpoint directory name
  when the checkpoint is created. May contain forward slashes. All intermediate directories
  will be automatically created. Defaults to the program name.
  \arg \c checkpoint-min-interval --- minimum duration between consecutive checkpoints
  (i.e. consecutive calls to \link MPI_Checkpoint_create\endlink). The following suffixes
  are supported: "s", "m", "h", "d" --- denoting seconds, minutes, hours, days respectively.
  Useful when you do not know how much time each iteration of the program takes.
  Default value is 0.
  \arg \c verbose --- print a message each time a checkpoint is created or restored.
  Default value is 0.
  \arg \c compression-level --- set compression level of the checkpoints.
  Maximum value is 9. Default value is 0 (compression is not used).
  */
int MPI_Checkpoint_init();

/**
  \brief Create a checkpoint for the current process.
  \details
  This function creates a checkpoint for the current process using its rank
  in the supplied communicator. Checkpoint is a file that contains opaque
  data that is only meaningful to the program that wrote this data
  to the file.
  \param[in] comm MPI communicator
  \param[out] checkpoint checkpoint handle that can be used to write the data to the file
  \return On success \c MPI_SUCCESS is returned. If the checkpoint was not
  created (e.g. it was disabled by environment variables or the time interval
  since the last checkpoint is too small) \c MPI_ERR_NO_CHECKPOINT is returned.
  If error occures, the correspoding \c MPI_ERR_* is returned.
  \see \link MPI_Checkpoint_init\endlink for the list of environment variables that affect
  this function.
  */
int MPI_Checkpoint_create(MPI_Comm comm, MPI_Checkpoint* checkpoint);

/**
  \brief Write the data to the checkpoint file.
  \details
  This function copies the data from the \p buffer to the internal buffer associated with the
  checkpoint file. The implementation tries to free old unused internal buffer memory.
  \param[in] checkpoint checkpoint handle that can be used to write the data to the file
  \param[in] buffer a pointer to the array of \p type
  \param[in] count the number of elements in the \p buffer
  \param[in] type the type of the buffer element
  \return On success \c MPI_SUCCESS is returned. On error the program is terminated.
  */
int MPI_Checkpoint_write(MPI_Checkpoint checkpoint, const void* buffer, int count, MPI_Datatype type);

/**
  \brief Flush the data to the checkpoint file.
  \details
  This function writes remaining data to the checkpoint file, closes the
  corresponding file descriptor and frees the memory.
  \param[in,out] checkpoint checkpoint handle
  \return On success \c MPI_SUCCESS is returned. On error the program is terminated.
  */
int MPI_Checkpoint_close(MPI_Checkpoint* checkpoint);

/**
  \brief Restore from the checkpoint.
  \details
  This function reads a checkpoint for the current process using its rank
  in the supplied communicator. Checkpoint is a file that contains opaque
  data that is only meaningful to the program that wrote this data
  to the file.
  \param[in] comm MPI communicator
  \param[out] checkpoint checkpoint handle that can be used to read the data from the file
  \return On success \c MPI_SUCCESS is returned. If the checkpoint was not
  created (e.g. it was disabled by environment variables or the time interval
  since the last checkpoint is too small) \c MPI_ERR_NO_CHECKPOINT is returned.
  If error occures, the correspoding \c MPI_ERR_* is returned.
  \see \link MPI_Checkpoint_init\endlink for the list of environment variables that affect
  this function.
  */
int MPI_Checkpoint_restore(MPI_Comm comm, MPI_Checkpoint* checkpoint);
/**
  \brief Read the data from the checkpoint file.
  \details
  This function copies the data from the internal buffer associated with the
  checkpoint file to \p buffer. The implementation tries to free old unused
  internal buffer memory.
  \param[in] checkpoint checkpoint handle that can be used to read the data from the file
  \param[in] buffer a pointer to the array of \p type
  \param[in] count the number of elements in the \p buffer
  \param[in] type the type of the buffer element
  \return On success \c MPI_SUCCESS is returned. On error the program is terminated.
  */
int MPI_Checkpoint_read(MPI_Checkpoint checkpoint, void* buffer, int count, MPI_Datatype type);

/**
  \brief Finalize the library.
  \details
  Deallocates compressor/decompressor buffers.
  \return On success \c MPI_SUCCESS is returned. On error \c MPI_ERR_OTHER is returned.
  */
int MPI_Checkpoint_finalize();

/**
  \brief Convert Fortran checkpoint handle to C checkpoint handle.
  \param[in] f_checkpoint Fortran checkpoint handle
  \return C checkpoint handle or \c MPI_CHECKPOINT_NULL if not found.
  */
MPI_Checkpoint MPI_Checkpoint_f2c(MPI_Fint f_checkpoint);

/**
  \brief Convert C checkpoint handle to Fortran checkpoint handle.
  \param[in] c_checkpoint C checkpoint handle
  \return Fortran checkpoint handle or \c MPI_CHECKPOINT_NULL
  (Fortran constant defined in \c mpi_checkpointf.h) if not found.
  */
MPI_Fint MPI_Checkpoint_c2f(MPI_Checkpoint c_checkpoint);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop

#endif // vim:filetype=c
