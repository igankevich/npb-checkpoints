!-------------------------------------------------------------------------!
!                                                                         !
!        N  A  S     P A R A L L E L     B E N C H M A R K S  3.4         !
!                                                                         !
!                                   B T                                   !
!                                                                         !
!-------------------------------------------------------------------------!
!                                                                         !
!    This benchmark is part of the NAS Parallel Benchmark 3.4 suite.      !
!    It is described in NAS Technical Reports 95-020 and 02-007.          !
!                                                                         !
!    Permission to use, copy, distribute and modify this software         !
!    for any purpose with or without fee is hereby granted.  We           !
!    request, however, that all derived work reference the NAS            !
!    Parallel Benchmarks 3.4. This software is provided "as is"           !
!    without express or implied warranty.                                 !
!                                                                         !
!    Information on NPB 3.4, including the technical report, the          !
!    original specifications, source code, results and information        !
!    on how to submit new results, is available at:                       !
!                                                                         !
!           http://www.nas.nasa.gov/Software/NPB/                         !
!                                                                         !
!    Send comments or suggestions to  npb@nas.nasa.gov                    !
!                                                                         !
!          NAS Parallel Benchmarks Group                                  !
!          NASA Ames Research Center                                      !
!          Mail Stop: T27A-1                                              !
!          Moffett Field, CA   94035-1000                                 !
!                                                                         !
!          E-mail:  npb@nas.nasa.gov                                      !
!          Fax:     (650) 604-3957                                        !
!                                                                         !
!-------------------------------------------------------------------------!

!---------------------------------------------------------------------
!
! Authors: R. F. Van der Wijngaart
!          T. Harris
!          M. Yarrow
!
!---------------------------------------------------------------------

!---------------------------------------------------------------------
       program MPBT
!---------------------------------------------------------------------

       use bt_data
       use mpinpb

       implicit none

       include 'mpi_checkpointf.h'

       integer i, niter, step, c, error, fstatus
       double precision navg, mflops, mbytes, n3

       external timer_read
       double precision t, tmax, tiominv, tpc, timer_read
       logical verified
       character class, cbuff*40
       double precision t1(t_last+2), tsum(t_last+2),  &
     &                  tming(t_last+2), tmaxg(t_last+2)
       character        t_recs(t_last+2)*8

       integer wr_interval
       integer checkpoint, step_min

       data t_recs/'total', 'i/o', 'rhs', 'xsolve', 'ysolve', 'zsolve',  &
     &             'bpack', 'exch', 'xcomm', 'ycomm', 'zcomm',  &
     &             ' totcomp', ' totcomm'/

       checkpoint = MPI_CHECKPOINT_NULL
       call setup_mpi
       if (.not. active) goto 999

!---------------------------------------------------------------------
!      Root node reads input file (if it exists) else takes
!      defaults from parameters
!---------------------------------------------------------------------
       if (node .eq. root) then

          write(*, 1000)

          call check_timer_flag( timeron )

          open (unit=2,file='inputbt.data',status='old', iostat=fstatus)
!
          rd_interval = 0
          if (fstatus .eq. 0) then
            write(*,233) 
 233        format(' Reading from input file inputbt.data')
            read (2,*) niter
            read (2,*) dt
            read (2,*) grid_points(1), grid_points(2), grid_points(3)
            if (iotype .ne. 0) then
                read (2,'(A)') cbuff
                read (cbuff,*,iostat=i) wr_interval, rd_interval
                if (i .ne. 0) rd_interval = 0
                if (wr_interval .le. 0) wr_interval = wr_default
            endif
            if (iotype .eq. 1) then
                read (2,*) collbuf_nodes, collbuf_size
                write(*,*) 'collbuf_nodes ', collbuf_nodes
                write(*,*) 'collbuf_size  ', collbuf_size
            endif
            close(2)
          else
            write(*,234) 
            niter = niter_default
            dt    = dt_default
            grid_points(1) = problem_size
            grid_points(2) = problem_size
            grid_points(3) = problem_size
            wr_interval = wr_default
            if (iotype .eq. 1) then
!             set number of nodes involved in collective buffering to 4,
!             unless total number of nodes is smaller than that.
!             set buffer size for collective buffering to 1MB per node
!             collbuf_nodes = min(4,no_nodes)
!             set default to No-File-Hints with a value of 0
              collbuf_nodes = 0
              collbuf_size = 1000000
            endif
          endif
 234      format(' No input file inputbt.data. Using compiled defaults')

          call set_class(niter, class)

          write(*, 1001) grid_points(1), grid_points(2), grid_points(3),  &
     &                   class
          write(*, 1002) niter, dt
          write(*, 1003) total_nodes
          if (no_nodes .ne. total_nodes) write(*, 1004) no_nodes
          write(*, *)

          if (iotype .eq. 1) write(*, 1006) 'FULL MPI-IO', wr_interval
          if (iotype .eq. 2) write(*, 1006) 'SIMPLE MPI-IO', wr_interval
          if (iotype .eq. 3) write(*, 1006) 'EPIO', wr_interval
          if (iotype .eq. 4) write(*, 1006) 'FORTRAN IO', wr_interval

 1000 format(//, ' NAS Parallel Benchmarks 3.4 -- BT Benchmark',/)
 1001     format(' Size: ', i4, 'x', i4, 'x', i4, '  (class ', a, ')' )
 1002     format(' Iterations: ', i4, '    dt: ', F11.7)
 1003     format(' Total number of processes: ', i6)
 1004     format(' WARNING: Number of processes is not a square number',  &
     &           ' (', i0, ' active)')
 1006     format(' BTIO -- ', A, ' write interval: ', i3 /)

       endif

       call mpi_bcast(niter, 1, MPI_INTEGER,  &
     &                root, comm_setup, error)

       call mpi_bcast(dt, 1, dp_type,  &
     &                root, comm_setup, error)

       call mpi_bcast(grid_points(1), 3, MPI_INTEGER,  &
     &                root, comm_setup, error)

       call mpi_bcast(wr_interval, 1, MPI_INTEGER,  &
     &                root, comm_setup, error)

       call mpi_bcast(rd_interval, 1, MPI_INTEGER,  &
     &                root, comm_setup, error)

       call mpi_bcast(timeron, 1, MPI_LOGICAL,  &
     &                root, comm_setup, error)

       call alloc_space

       call make_set

       do  c = 1, maxcells
          if ( (cell_size(1,c) .gt. IMAX) .or.  &
     &         (cell_size(2,c) .gt. JMAX) .or.  &
     &         (cell_size(3,c) .gt. KMAX) ) then
             print *,node, c, (cell_size(i,c),i=1,3)
             print *,' Problem size too big for compiled array sizes'
             goto 999
          endif
       end do

       do  i = 1, t_last
          call timer_clear(i)
       end do

       call set_constants

       call initialize

       call setup_btio
       idump = 0

       call lhsinit

       call exact_rhs

       call compute_buffer_size(5)

!---------------------------------------------------------------------
!      do one time step to touch all code, and reinitialize
!---------------------------------------------------------------------
       call adi
       call initialize

!---------------------------------------------------------------------
!      Synchronize before placing time stamp
!---------------------------------------------------------------------
       do  i = 1, t_last
          call timer_clear(i)
       end do
       call mpi_barrier(comm_setup, error)

       call timer_start(1)

       step_min = 1
       call mpi_checkpoint_restore(comm_setup, checkpoint, error)
       if (error .eq. 0) then
           call mpi_checkpoint_read(checkpoint, step_min, 1, MPI_INTEGER, error)
           call mpi_checkpoint_read(checkpoint, u, size(u), MPI_DOUBLE_PRECISION, error)
           call mpi_checkpoint_read(checkpoint, rhs, size(rhs), MPI_DOUBLE_PRECISION, error)
           call mpi_checkpoint_close(checkpoint, error)
       endif

       do  step = step_min, niter

          if (step .eq. niter/2 .and. step_min .eq. 1) then
              call mpi_checkpoint_create(comm_setup, checkpoint, error)
              if (error .eq. 0) then
                  call mpi_checkpoint_write(checkpoint, step, 1, MPI_INTEGER, error)
                  call mpi_checkpoint_write(checkpoint, u, size(u), MPI_DOUBLE_PRECISION, error)
                  call mpi_checkpoint_write(checkpoint, rhs, size(rhs), MPI_DOUBLE_PRECISION, error)
                  call mpi_checkpoint_close(checkpoint, error)
              endif
          endif

          if (node .eq. root) then
             if (mod(step, 20) .eq. 0 .or. step .eq. niter .or.  &
     &           step .eq. 1) then
                write(*, 200) step
 200            format(' Time step ', i4)
             endif
          endif

          call adi

          if (iotype .ne. 0) then
              if (mod(step, wr_interval).eq.0 .or. step .eq. niter) then
                  if (node .eq. root) then
                      print *, 'Writing data set, time step', step
                  endif
                  if (step .eq. niter .and. rd_interval .gt. 1) then
                      rd_interval = 1
                  endif
                  call timer_start(2)
                  call output_timestep
                  call timer_stop(2)
                  idump = idump + 1
              endif
          endif

       end do

       call timer_start(2)
       call btio_cleanup
       call timer_stop(2)

       call timer_stop(1)
       t = timer_read(1)

       call verify(class, verified)

       call mpi_reduce(t, tmax, 1,  &
     &                 dp_type, MPI_MAX,  &
     &                 root, comm_setup, error)

       if (iotype .ne. 0) then
          t = timer_read(2)
          if (t .ne. 0.d0) t = 1.0d0 / t
          call mpi_reduce(t, tiominv, 1,  &
     &                    dp_type, MPI_SUM,  &
     &                    root, comm_setup, error)
       endif

       if( node .eq. root ) then
          n3 = dble(grid_points(1))*grid_points(2)*grid_points(3)
          navg = (grid_points(1)+grid_points(2)+grid_points(3))/3.d0
          if( tmax .ne. 0. ) then
             mflops = 1.0d-6*dble(niter)*  &
     &                (3478.8*n3-17655.7*navg**2+28023.7*navg)  &
     &                / tmax
          else
             mflops = 0.d0
          endif

          if (iotype .ne. 0) then
             mbytes = n3 * 40.0 * idump * 1.0d-6
             tiominv = tiominv / no_nodes
             t = 0.0
             if (tiominv .ne. 0.) t = 1.d0 / tiominv
             tpc = 0.0
             if (tmax .ne. 0.) tpc = t * 100.0 / tmax
             write(*,1100) t, tpc, mbytes, mbytes*tiominv
 1100        format(/' BTIO -- statistics:'/  &
     &               '   I/O timing in seconds   : ', f14.2/  &
     &               '   I/O timing percentage   : ', f14.2/  &
     &               '   Total data written (MB) : ', f14.2/  &
     &               '   I/O data rate  (MB/sec) : ', f14.2)
          endif

         call print_results('BT', class, grid_points(1),  &
     &     grid_points(2), grid_points(3), niter, no_nodes,  &
     &     total_nodes, tmax, mflops, '          floating point',  &
     &     verified, npbversion,compiletime, cs1, cs2, cs3, cs4, cs5,  &
     &     cs6, '(none)')
       endif

       if (.not.timeron) goto 999

       do i = 1, t_last
          t1(i) = timer_read(i)
       end do
       t1(t_xsolve) = t1(t_xsolve) - t1(t_xcomm)
       t1(t_ysolve) = t1(t_ysolve) - t1(t_ycomm)
       t1(t_zsolve) = t1(t_zsolve) - t1(t_zcomm)
       t1(t_last+2) = t1(t_xcomm)+t1(t_ycomm)+t1(t_zcomm)+t1(t_exch)
       t1(t_last+1) = t1(t_total)  - t1(t_last+2)

       call MPI_Reduce(t1, tsum,  t_last+2, dp_type, MPI_SUM,  &
     &                 0, comm_setup, error)
       call MPI_Reduce(t1, tming, t_last+2, dp_type, MPI_MIN,  &
     &                 0, comm_setup, error)
       call MPI_Reduce(t1, tmaxg, t_last+2, dp_type, MPI_MAX,  &
     &                 0, comm_setup, error)

       if (node .eq. 0) then
          write(*, 800) no_nodes
          do i = 1, t_last+2
             tsum(i) = tsum(i) / no_nodes
             write(*, 810) i, t_recs(i), tming(i), tmaxg(i), tsum(i)
          end do
       endif
 800   format(' nprocs =', i6, 11x, 'minimum', 5x, 'maximum',  &
     &        5x, 'average')
 810   format(' timer ', i2, '(', A8, ') :', 3(2x,f10.4))

 999   continue
       call mpi_barrier(MPI_COMM_WORLD, error)
       call mpi_finalize(error)
       call mpi_checkpoint_finalize(error)

       end

