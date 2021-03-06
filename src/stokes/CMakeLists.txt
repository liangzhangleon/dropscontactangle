set(HOME stokes)
libs(instatstokes2phase stokes)
libs_ser(integrTime)

target_link_libraries_par(stokes-instatstokes2phase num-discretize)

if(NOT MPI)
    add_library(stokes-stokesCoeff SHARED stokesCoeff)
    #set_property(TARGET stokes-stokesCoeff PROPERTY SUFFIX "")
    if(NOT WIN32)
        target_link_libraries(stokes-stokesCoeff)
    endif(NOT WIN32)
endif()

exec_ser(sdrops geom-boundary geom-builder geom-deformation geom-simplex geom-multigrid misc-scopetimer misc-progressaccu misc-params num-unknowns geom-topo num-fe misc-problem num-interfacePatch misc-utils num-discretize out-output geom-principallattice geom-reftetracut stokes-stokes)

exec_ser(sdropsP2 geom-boundary geom-builder geom-deformation geom-simplex
geom-multigrid misc-scopetimer misc-progressaccu misc-params num-unknowns
geom-topo num-fe misc-problem misc-scopetimer num-interfacePatch misc-utils
out-output num-discretize num-MGsolver misc-params out-ensightOut out-vtkOut
stokes-integrTime geom-geomselect misc-funcmap geom-principallattice
geom-reftetracut num-oseenprecond misc-dynamicload stokes-stokes)
if(NOT MPI)
add_dependencies(sdropsP2 misc-vectorFunctions misc-scalarFunctions stokes-stokesCoeff)
endif(NOT MPI)

exec_ser(errorestimator geom-boundary geom-builder geom-deformation geom-simplex geom-multigrid misc-scopetimer misc-progressaccu misc-params num-unknowns geom-topo num-fe misc-problem  num-interfacePatch misc-utils num-discretize num-MGsolver misc-params stokes-integrTime  geom-geomselect out-output misc-funcmap geom-principallattice geom-reftetracut num-oseenprecond misc-dynamicload stokes-stokes)
if(NOT MPI)
add_dependencies(errorestimator misc-scalarFunctions misc-vectorFunctions stokes-stokesCoeff)
endif(NOT MPI)

add_my_custom_targets(stokes)
