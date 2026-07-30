foreach(D IN LISTS AMReX_SPACEDIM)
    set(_sources main.cpp)
    set(_input_files )

    setup_test(${D} _sources _input_files)

    if (AMReX_MPI)
        set_tests_properties(
            Algebra_SpGEMM_${D}d PROPERTIES PROCESSORS 2
        )
        set(_target Test_Algebra_SpGEMM_${D}d)
        foreach(_ntasks IN ITEMS 1 4)
            add_test(
                NAME Algebra_SpGEMM_${D}d_mpi_${_ntasks}
                COMMAND
                    ${MPIEXEC_EXECUTABLE}
                    ${MPIEXEC_NUMPROC_FLAG} ${_ntasks}
                    ${MPIEXEC_PREFLAGS}
                    $<TARGET_FILE:${_target}>
                    ${MPIEXEC_POSTFLAGS}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${D}d
            )
            set_tests_properties(
                Algebra_SpGEMM_${D}d_mpi_${_ntasks}
                PROPERTIES PROCESSORS ${_ntasks}
            )
        endforeach()
    endif()

    unset(_sources)
    unset(_input_files)
endforeach()
