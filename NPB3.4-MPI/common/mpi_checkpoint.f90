module mpi_checkpoint

    interface mpi_checkpoint_create
        module procedure mpi_checkpoint_create
    end interface

    interface mpi_checkpoint_restore
        module procedure mpi_checkpoint_restore
    end interface

    interface mpi_checkpoint_close
        module procedure mpi_checkpoint_close
    end interface

        !subroutine mpi_checkpoint_create(comm,filename,fh,ierr)
        !    integer, intent(in) :: comm
        !    character*(*), intent(in) :: filename
        !    integer, intent(out) :: ierr, fh
        !end subroutine

        !subroutine mpi_checkpoint_restore(comm,filename,fh,ierr)
        !    integer, intent(in) :: comm, fh
        !    integer, intent(out) :: ierr
        !end subroutine

        !subroutine mpi_checkpoint_close(file,ierr)
        !    integer, intent(in) :: file
        !    integer, intent(out) :: ierr
        !end subroutine

    contains

        subroutine mpi_checkpoint_create(comm,filename,fh,ierr)
            use mpi
            integer comm, ierr, fh
            character*(*) filename
            call mpi_file_open(comm,filename,MPI_MODE_CREATE+MPI_MODE_WRONLY,MPI_INFO_NULL,fh,ierr)
        end subroutine

        subroutine mpi_checkpoint_restore(comm,filename,fh,ierr)
            use mpi
            integer comm, ierr, fh
            character*(*) filename
            call mpi_file_open(comm,filename,MPI_MODE_RDONLY,MPI_INFO_NULL,fh,ierr)
        end subroutine

        subroutine mpi_checkpoint_close(file,ierr)
            use mpi
            integer ierr, file
            call mpi_file_close(file,ierr)
            !write (*,*) 'Checkpoint finished!'
        end subroutine

end module

