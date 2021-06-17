module mpi_checkpoint

    interface mpi_checkpoint_create
        module procedure mpi_checkpoint_create
    end interface

    interface mpi_checkpoint_restore
        module procedure mpi_checkpoint_restore
        module procedure mpi_checkpoint_restore_filename
    end interface

    interface mpi_checkpoint_close
        module procedure mpi_checkpoint_close
    end interface

    contains

        ! https://stackoverflow.com/questions/29455075/automatic-file-and-folder-naming
        function timestamp_filename(prefix) result(str)
            character*(*) prefix
            character(5) zone
            integer :: values(8)
            character(len=4096) :: str
            call date_and_time(values=values,zone=zone)
            write(str,10) prefix, values(1), values(2), values(3), values(5), values(6), values(7), zone
10          format(a,'_',i4.4,'-',i2.2,'-',i2.2,'T',i2.2,'_',i2.2,'_',i2.2,a,'.checkpoint')
        end function

        subroutine mpi_checkpoint_create(comm,filename,fh,ierr)
            use mpi
            integer comm, ierr, fh, rank, unused
            character*(*) filename
            character(4096) newfilename
            newfilename = timestamp_filename(filename)
            call mpi_file_open(comm,newfilename,MPI_MODE_CREATE+MPI_MODE_WRONLY,MPI_INFO_NULL,fh,ierr)
            if (ierr .eq. 0) then
                call mpi_comm_rank(comm,rank,unused)
                write (*,20) rank, trim(newfilename)
20              format('rank ',i3,' creating ',a)
            endif
        end subroutine

        subroutine mpi_checkpoint_restore(comm,fh,ierr)
            integer comm, ierr, fh
            character(4096) filename
            call getenv('MPI_CHECKPOINT', filename)
            call mpi_checkpoint_restore_filename(comm,trim(filename),fh,ierr)
        end subroutine

        subroutine mpi_checkpoint_restore_filename(comm,filename,fh,ierr)
            use mpi
            integer comm, ierr, fh, rank, unused
            character*(*) filename
            call mpi_file_open(comm,filename,MPI_MODE_RDONLY,MPI_INFO_NULL,fh,ierr)
            if (ierr .eq. 0) then
                call mpi_comm_rank(comm,rank,unused)
                write (*,20) rank, filename
20              format('rank ',i3,' restored from ',a)
            endif
        end subroutine

        subroutine mpi_checkpoint_close(file,ierr)
            use mpi
            integer ierr, file
            call mpi_file_close(file,ierr)
        end subroutine

end module

