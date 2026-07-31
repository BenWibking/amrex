foreach(D IN LISTS AMReX_SPACEDIM)
    # The high-contrast nonlinear FLD tolerances require double precision.
    if (D EQUAL 2 AND AMReX_PRECISION STREQUAL "DOUBLE")
        set(_sources main.cpp)
        set(_input_files )

        setup_test(${D} _sources _input_files)

        if (AMReX_MPI)
            set_tests_properties(
                Algebra_FLD_${D}d PROPERTIES PROCESSORS 2
            )
            set(_target Test_Algebra_FLD_${D}d)
            foreach(_ntasks IN ITEMS 1 4)
                add_test(
                    NAME Algebra_FLD_${D}d_mpi_${_ntasks}
                    COMMAND
                        ${MPIEXEC_EXECUTABLE}
                        ${MPIEXEC_NUMPROC_FLAG} ${_ntasks}
                        ${MPIEXEC_PREFLAGS}
                        $<TARGET_FILE:${_target}>
                        ${MPIEXEC_POSTFLAGS}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${D}d
                )
                set_tests_properties(
                    Algebra_FLD_${D}d_mpi_${_ntasks}
                    PROPERTIES PROCESSORS ${_ntasks}
                )
            endforeach()
        endif()

        unset(_sources)
        unset(_input_files)
    endif()
endforeach()
