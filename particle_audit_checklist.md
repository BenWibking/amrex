# `Src/Particle` Function Audit

This is the running manual-audit checklist for `Src/Particle`. A checked item
means the complete function body (including conditional compilation paths) was
read and reviewed for correctness. Candidate findings link to one issue note in
`issues/` per bug.

## Progress

- Audited function-like definitions: 946
- Candidate bugs recorded: 94
- Audit status: complete

## Audited

### `AMReX_MakeParticle.H`

- [x] `make_particle<T_ParticleType>::operator()` (AoS overload)
- [x] `make_particle<T_ParticleType>::operator()` (SoA specialization)

No candidate bugs found.

### `AMReX_ParticleMPIUtil.H` / `AMReX_ParticleMPIUtil.cpp`

- [x] `CountSnds`
- [x] `doHandShake`
- [x] `doHandShakeLocal`

No candidate bugs found. The vectors are sized and zero-initialized by the
current caller before the handshake routines use rank-indexed entries.

### `AMReX_ParticleUtil.cpp`

- [x] `computeRefFac`
- [x] `computeNeighborProcs` --
  [`issues/compute-neighbor-procs-amr-growth.md`](issues/compute-neighbor-procs-amr-growth.md)

The header definitions are checked in their dedicated section below.

### `AMReX_ParticleHeader.H` / `AMReX_ParticleHeader.cpp`

- [x] `ParticleHeader::parse`
- [x] `ParticleHeader::read`

No candidate bugs found for well-formed AMReX particle headers. Malformed-file
hardening is not treated as a correctness bug in this pass.

### `AMReX_TracerParticles.H` / `AMReX_TracerParticles.cpp`

- [x] `TracerParticleContainer::TracerParticleContainer(ParGDBBase*)`
- [x] `TracerParticleContainer::TracerParticleContainer(Geometry, DistributionMapping, BoxArray)`
- [x] `TracerParticleContainer::TracerParticleContainer()`
- [x] `TracerParticleContainer::~TracerParticleContainer()`
- [x] deleted copy constructor
- [x] deleted copy-assignment operator
- [x] defaulted move constructor
- [x] defaulted move-assignment operator
- [x] `TracerParticleContainer::AdvectWithUmac`
- [x] `TracerParticleContainer::AdvectWithUcc`
- [x] `TracerParticleContainer::Timestamp` --
  [`issues/tracer-timestamp-parallel-context.md`](issues/tracer-timestamp-parallel-context.md),
  [`issues/tracer-timestamp-component-bounds.md`](issues/tracer-timestamp-component-bounds.md),
  [`issues/tracer-timestamp-grid-layout.md`](issues/tracer-timestamp-grid-layout.md)

### `AMReX_Particles.H`

- [x] No function definitions (umbrella include only)

### `AMReX_ArrayOfStructs.H`

- [x] `operator()` (const and mutable overloads)
- [x] `size`
- [x] `numParticles`
- [x] `numRealParticles`
- [x] `numNeighborParticles`
- [x] `numTotalParticles`
- [x] `setNumNeighbors`
- [x] `getNumNeighbors`
- [x] `empty` (const and mutable overloads)
- [x] `data` (const and mutable overloads)
- [x] `dataPtr` (const and mutable overloads)
- [x] `dataShape`
- [x] `push_back`
- [x] `pop_back`
- [x] `back` (const and mutable overloads)
- [x] `operator[]` (const and mutable overloads)
- [x] `swap`
- [x] `resize`
- [x] `reserve`
- [x] `erase`
- [x] `insert`
- [x] `begin`, `cbegin` (const and mutable overloads)
- [x] `end`, `cend` (const and mutable overloads)
- [x] `collectVectors`

No candidate bugs found. The class relies on the surrounding particle-tile
contract to keep neighbor counts consistent when resizing.

### `AMReX_BinIterator.H`

- [x] `IsParticleTileData` (detection and fallback overloads)
- [x] `BinIterator::iterator::iterator`
- [x] `BinIterator::iterator::operator++`
- [x] `BinIterator::iterator::operator!=`
- [x] `BinIterator::iterator::operator*`
- [x] `BinIterator::begin`
- [x] `BinIterator::end`
- [x] `BinIterator::BinIterator`

No candidate bugs found. The deliberately minimal iterator operations satisfy
the internal range-for use.

### `AMReX_DenseBins.H`

- [x] `DenseBinIteratorFactory::DenseBinIteratorFactory` --
  [`issues/dense-bin-factory-particle-tile-data.md`](issues/dense-bin-factory-particle-tile-data.md)
- [x] `DenseBinIteratorFactory::getBinIterator`
- [x] `DenseBins::call_f`
- [x] default-policy `DenseBins::build` (`Box` and integer-bin overloads)
- [x] GPU-policy `DenseBins::build` (`Box` and integer-bin overloads) --
  [`issues/dense-bins-zero-bin-build.md`](issues/dense-bins-zero-bin-build.md)
- [x] OpenMP-policy `DenseBins::build` (`Box` and integer-bin overloads) --
  [`issues/dense-bins-zero-bin-build.md`](issues/dense-bins-zero-bin-build.md)
- [x] serial-policy `DenseBins::build` (`Box` and integer-bin overloads) --
  [`issues/dense-bins-zero-bin-build.md`](issues/dense-bins-zero-bin-build.md)
- [x] `numItems`
- [x] `numBins` --
  [`issues/bin-containers-empty-num-bins.md`](issues/bin-containers-empty-num-bins.md)
- [x] `permutationPtr` (const and mutable overloads)
- [x] `offsetsPtr` (const and mutable overloads)
- [x] `binsPtr` (const and mutable overloads)
- [x] `getBinIteratorFactory`

### `AMReX_SparseBins.H`

- [x] `SparseBinIteratorFactory::SparseBinIteratorFactory`
- [x] `SparseBinIteratorFactory::getIndex` --
  [`issues/sparse-bin-single-entry-lookup.md`](issues/sparse-bin-single-entry-lookup.md)
- [x] `SparseBinIteratorFactory::getBinIterator`
- [x] `SparseBins::build`
- [x] `numItems`
- [x] `numBins` --
  [`issues/bin-containers-empty-num-bins.md`](issues/bin-containers-empty-num-bins.md)
- [x] `permutationPtr` (const and mutable overloads)
- [x] `offsetsPtr` (const and mutable overloads)
- [x] `getNonZeroBinsPtr` (const and mutable overloads)
- [x] `getBinIteratorFactory`

### `AMReX_ParticleBufferMap.H` / `AMReX_ParticleBufferMap.cpp`

- [x] `GetPID::GetPID`
- [x] `GetPID::operator()`
- [x] `GetBucket::GetBucket`
- [x] `GetBucket::operator()`
- [x] `ParticleBufferMap::ParticleBufferMap()`
- [x] `ParticleBufferMap::ParticleBufferMap(ParGDBBase*)`
- [x] tiled `ParticleBufferMap::ParticleBufferMap`
- [x] `ParticleBufferMap::define(ParGDBBase*)`
- [x] tiled `ParticleBufferMap::define`
- [x] `ParticleBufferMap::isValid(ParGDBBase*)`
- [x] tiled `ParticleBufferMap::isValid`
- [x] `numLevels`
- [x] `numBuckets`
- [x] `bucketToGrid`
- [x] `bucketToTile`
- [x] `bucketToLevel`
- [x] `bucketToProc`
- [x] `gridAndLevToBucket`
- [x] `gridAndTileAndLevToBucket`
- [x] `firstBucketOnProc`
- [x] `numBoxesOnProc`
- [x] `allBucketsOnProc`
- [x] `procID` (grid/level and grid/tile/level overloads)
- [x] `getPIDFunctor`
- [x] `getBucketFunctor`
- [x] `getHostBucketFunctor`

No candidate bugs found. Rank translation assumes the documented current
parallel context owns every rank in the supplied particle distribution maps.

### `AMReX_ParticleArray.H`

- [x] `ref_wrapper` constructors, destructor, and defaulted special members
- [x] `ref_wrapper::operator=(T&&)` --
  [`issues/particle-array-lvalue-assignment.md`](issues/particle-array-lvalue-assignment.md)
- [x] `ref_wrapper::operator T&`
- [x] `ref_wrapper::get`
- [x] AoS `DataLayoutPolicy::get_raw_data`
- [x] AoS `DataLayoutPolicy::resize`
- [x] AoS `DataLayoutPolicy::push_back`
- [x] AoS `DataLayoutPolicy::size`
- [x] AoS `DataLayoutPolicyRaw::get`
- [x] SoA `DataLayoutPolicy::get_raw_data` and `get_raw_data_impl` --
  [`issues/particle-array-empty-soa-accessor.md`](issues/particle-array-empty-soa-accessor.md)
- [x] SoA `DataLayoutPolicy::resize` and `resize_impl`
- [x] SoA `DataLayoutPolicy::push_back` and `push_back_impl`
- [x] SoA `DataLayoutPolicy::size`
- [x] SoA `DataLayoutPolicyRaw::get` and `get_impl`
- [x] `ParticleArray` constructors
- [x] `ParticleArray::push_back`
- [x] `ParticleArray::size`
- [x] `ParticleArray::resize`
- [x] `ParticleArray::get_particle_data`
- [x] `ParticleArrayAccessor::ParticleArrayAccessor`
- [x] `ParticleArrayAccessor::operator[]`
- [x] `ParticleArrayAccessor::size`

### `AMReX_StructOfArrays.H`

- [x] `StructOfArrays::define`
- [x] `NumRealComps`
- [x] `NumIntComps`
- [x] `GetIdCPUData` (const and mutable overloads)
- [x] compile-time-array `GetRealData` (const and mutable overloads)
- [x] compile-time-array `GetIntData` (const and mutable overloads)
- [x] `GetRealNames`
- [x] `GetIntNames`
- [x] indexed `GetRealData` (const and mutable overloads)
- [x] named `GetRealData` (const and mutable overloads)
- [x] indexed `GetIntData` (const and mutable overloads)
- [x] named `GetIntData` (const and mutable overloads)
- [x] `size`
- [x] `empty`
- [x] `numParticles`
- [x] `numRealParticles`
- [x] `numNeighborParticles`
- [x] `numTotalParticles`
- [x] `setNumNeighbors`
- [x] `getNumNeighbors`
- [x] `resize`
- [x] `reserve`
- [x] `idcpuarray`
- [x] `realarray` --
  [`issues/struct-of-arrays-particle-real-type.md`](issues/struct-of-arrays-particle-real-type.md)
- [x] `intarray`
- [x] `collectVectors`

### `AMReX_ParticleMesh.H`

- [x] both `particle_detail::call_f` overloads
- [x] `ParticleToMesh` --
  [`issues/particle-mesh-temporary-staggering.md`](issues/particle-mesh-temporary-staggering.md)
- [x] `MeshToParticle` --
  [`issues/particle-mesh-temporary-staggering.md`](issues/particle-mesh-temporary-staggering.md)

### `AMReX_ParticleInterpolators.H`

- [x] `ParticleInterpolator::Base::ParticleToMesh`
- [x] `ParticleInterpolator::Base::MeshToParticle`
- [x] `ParticleInterpolator::Nearest::Nearest` --
  [`issues/nearest-particle-interpolator-half-cell.md`](issues/nearest-particle-interpolator-half-cell.md)
- [x] `ParticleInterpolator::Linear::Linear`

### `AMReX_ParGDB.H`

- [x] `ParGDBBase` defaulted constructor, destructor, copy/move constructors,
  and copy/move assignments
- [x] `ParGDBBase::OnSameGrids`
- [x] `ParGDB::ParGDB()` --
  [`issues/pargdb-default-uninitialized-levels.md`](issues/pargdb-default-uninitialized-levels.md)
- [x] all three populated `ParGDB` constructors
- [x] level and vector overloads of `Geom` and `ParticleGeom`
- [x] level and vector overloads of `DistributionMap` and
  `ParticleDistributionMap`
- [x] level and vector overloads of `boxArray` and `ParticleBoxArray`
- [x] `SetParticleBoxArray`
- [x] `SetParticleDistributionMap`
- [x] `SetParticleGeometry`
- [x] `ClearParticleBoxArray`
- [x] `ClearParticleDistributionMap`
- [x] `ClearParticleGeometry`
- [x] `LevelDefined` --
  [`issues/pargdb-negative-level-defined.md`](issues/pargdb-negative-level-defined.md)
- [x] `finestLevel`
- [x] `maxLevel`
- [x] level and vector overloads of `refRatio`
- [x] `MaxRefRatio` --
  [`issues/pargdb-max-ref-ratio-ignores-level.md`](issues/pargdb-max-ref-ratio-ignores-level.md)

The pure-virtual interface declarations were also checked against the concrete
`ParGDB` implementations.

### `AMReX_ParIter.H`

- [x] both `ParIterBase_impl` constructors
- [x] `ParIterBase_impl::operator++`
- [x] `GetParticleTile`
- [x] `GetArrayOfStructs`
- [x] `GetStructOfArrays`
- [x] `numParticles`
- [x] `numRealParticles`
- [x] `numNeighborParticles`
- [x] `capacity`
- [x] `GetLevel`
- [x] `GetPairIndex`
- [x] `Geom`
- [x] both `ParIter_impl` constructors
- [x] both `ParConstIter_impl` constructors

No candidate bugs found. Skipping tiles with zero real particles is consistent
with the iterator's traversal contract; neighbor counts are exposed for tiles
that also contain real particles.

### `AMReX_Particle.H`

- [x] `particle_impl::unpack_id`
- [x] `particle_impl::unpack_cpu`
- [x] `particle_impl::pack_id`
- [x] `particle_impl::pack_cpu`
- [x] scalar and masked `particle_impl::make_invalid`
- [x] scalar and masked `particle_impl::make_valid`
- [x] `particle_impl::is_valid`
- [x] all `ParticleIDWrapper` constructors, destructor, assignments, conversion,
  validity mutators, and `is_valid`
- [x] all `ParticleCPUWrapper` constructors, destructor, assignments, and
  conversion
- [x] `ConstParticleIDWrapper` constructor, conversion, and `is_valid`
- [x] `ConstParticleCPUWrapper` constructor and conversion
- [x] `SetParticleIDandCPU`
- [x] mutable and const `Particle::cpu`
- [x] mutable and const `Particle::id`
- [x] `Particle::atomicSetID` --
  [`issues/particle-atomic-set-id-clears-cpu.md`](issues/particle-atomic-set-id-clears-cpu.md)
- [x] mutable and const `Particle::idcpu`
- [x] all `Particle::pos`, `rdata`, `rvec`, and `idata` overloads --
  [`issues/particle-negative-component-index.md`](issues/particle-negative-component-index.md)
- [x] `Particle::NextID()`
- [x] `Particle::UnprotectedNextID`
- [x] `Particle::NextID(Long)`
- [x] all four `operator<<` overloads

### `AMReX_Particle_mod_K.H`

- [x] `amrex_deposit_cic` (all dimensional branches)
- [x] `amrex_deposit_particle_dx_cic` (all dimensional branches)

No candidate bugs found. Both kernels assume the caller supplies enough FAB
grow cells and component storage for the requested deposition stencil.

### `AMReX_TracerParticle_mod_K.H`

- [x] `cic_interpolate`
- [x] `cic_interpolate_cc`
- [x] `cic_interpolate_nd`
- [x] `mac_interpolate`
- [x] `linear_interpolate_to_particle` (all dimensional branches)
- [x] `cic_interpolate_mapped_z`
- [x] `cic_interpolate_cc_mapped_z`
- [x] `cic_interpolate_nd_mapped_z`
- [x] `mac_interpolate_mapped_z`
- [x] `linear_interpolate_to_particle_z` (1D rejection plus 2D/3D branches)
- [x] `particle_interp_decomp`
- [x] `particle_interp_solve`
- [x] `cic_interpolate_nd_mapped`
- [x] `linear_interpolate_to_particle_mapped` (1D rejection plus 2D/3D branches)

No candidate bugs found for nondegenerate mapped cells and valid, sufficiently
grown mesh arrays. The mapped routines intentionally rely on particle integer
components to cache the enclosing vertical/general-mapped cell.

### `AMReX_ParticleLocator.H`

- [x] `AssignGrid::AssignGrid()`
- [x] populated `AssignGrid::AssignGrid`
- [x] `AssignGrid::getTile`
- [x] particle and `IntVect` overloads of `AssignGrid::operator()`
- [x] `ParticleLocator::ParticleLocator`
- [x] `ParticleLocator::build`
- [x] `ParticleLocator::setGeometry`
- [x] `ParticleLocator::getGridAssignor`
- [x] `ParticleLocator::isValid`
- [x] `AmrAssignGrid::AmrAssignGrid`
- [x] `AmrAssignGrid::operator()` --
  [`issues/amr-assign-grid-level-bounds.md`](issues/amr-assign-grid-level-bounds.md)
- [x] all three `AmrParticleLocator` constructors
- [x] vector and `ParGDBBase` overloads of `AmrParticleLocator::build` --
  [`issues/amr-particle-locator-vector-size.md`](issues/amr-particle-locator-vector-size.md)
- [x] vector and `ParGDBBase` overloads of `AmrParticleLocator::isValid`
- [x] `AmrParticleLocator::setGeometry`
- [x] `AmrParticleLocator::getGridAssignor`

### `AMReX_ParticleTransformation.H`

- [x] both classic `copyParticle` overloads
- [x] classic `swapParticle`
- [x] runtime-only `copyParticle`
- [x] runtime-only `swapParticle`
- [x] both `copyParticles` overloads
- [x] both single-destination `transformParticles` overloads
- [x] both two-destination `transformParticles` overloads
- [x] mask-based `filterParticles` convenience overload --
  [`issues/filter-particles-undeduced-template.md`](issues/filter-particles-undeduced-template.md)
- [x] ranged mask-based `filterParticles` --
  [`issues/particle-filter-empty-range.md`](issues/particle-filter-empty-range.md)
- [x] both predicate-based `filterParticles` overloads
- [x] mask-based `filterAndTransformParticles` with and without offsets --
  [`issues/particle-filter-empty-range.md`](issues/particle-filter-empty-range.md),
  [`issues/filter-transform-source-offset.md`](issues/filter-transform-source-offset.md)
- [x] predicate-based single-destination `filterAndTransformParticles` with
  and without offsets --
  [`issues/filter-transform-source-offset.md`](issues/filter-transform-source-offset.md)
- [x] mask- and predicate-based two-destination
  `filterAndTransformParticles` --
  [`issues/particle-filter-empty-range.md`](issues/particle-filter-empty-range.md)
- [x] `gatherParticles`
- [x] `scatterParticles`

### `AMReX_ParticleTileRT.H`

- [x] all `ArrayView` access and iterator functions --
  [`issues/particle-tile-rt-index-bounds.md`](issues/particle-tile-rt-index-bounds.md)
- [x] all three `ParticleTileDataRT` constructors
- [x] `ParticleTileDataRT::pos`, `id`, `cpu`, and `idcpu` --
  [`issues/particle-tile-rt-index-bounds.md`](issues/particle-tile-rt-index-bounds.md)
- [x] pointer and scalar `ParticleTileDataRT::rdata`/`idata` overloads --
  [`issues/particle-tile-rt-index-bounds.md`](issues/particle-tile-rt-index-bounds.md)
- [x] `ParticleTileDataRT::operator[]`
- [x] `ParticleTileDataRT::packParticleData`
- [x] `ParticleTileDataRT::unpackParticleData`
- [x] `ParticleTileDataRT::getSuperParticle`
- [x] both `RTSoAParticle` constructors
- [x] `RTSoAParticle::cpu`, `id`, `idcpu`, `rdata`, `idata`, and both `pos`
  overloads
- [x] `RTSoAParticle::NextID()`
- [x] `RTSoAParticle::UnprotectedNextID`
- [x] `RTSoAParticle::NextID(Long)`
- [x] all `ParticleTileRT` defaulted/deleted special members
- [x] `ParticleTileRT::define` --
  [`issues/particle-tile-rt-negative-sizes.md`](issues/particle-tile-rt-negative-sizes.md)
- [x] `empty`, `size`, `numParticles`, `numRealParticles`,
  `numNeighborParticles`, `numTotalParticles`, and `getNumNeighbors`
- [x] all four component-count accessors
- [x] `GetRealNames` and `GetIntNames`
- [x] const and mutable `GetIdCPUData`
- [x] indexed and named, const and mutable `GetRealData`/`GetIntData`
- [x] `resize`, both `reserve` overloads, and `realloc_and_move` --
  [`issues/particle-tile-rt-negative-sizes.md`](issues/particle-tile-rt-negative-sizes.md)
- [x] `shrink_to_fit`
- [x] `capacity`
- [x] `swap`
- [x] `getParticleTileData` and `getConstParticleTileData`
- [x] const and mutable `GetStructOfArrays`
- [x] `arena`
- [x] `get_idx_from_str`
- [x] `align_capacity`

### `AMReX_ParticleTile.H`

- [x] all mutable `ParticleTileData` accessors and `operator[]`
- [x] mutable `ParticleTileData::packParticleData` and `unpackParticleData`
- [x] both mutable `ParticleTileData::getSuperParticle` overloads
- [x] both mutable `ParticleTileData::setSuperParticle` overloads
- [x] all `ConstSoAParticle` accessors and its constructor
- [x] all mutable and const `SoAParticle` accessors and its constructor
- [x] `SoAParticle::NextID()`
- [x] `SoAParticle::UnprotectedNextID`
- [x] `SoAParticle::NextID(Long)`
- [x] all const `ConstParticleTileData` accessors and `operator[]`
- [x] `ConstParticleTileData::packParticleData`
- [x] both `ConstParticleTileData::getSuperParticle` overloads
- [x] all `RuntimePtrCacheDirtyFlag` special members, assignments, `store`, and
  `load`
- [x] all `ParticleTile` defaulted/deleted special members
- [x] `ParticleTile::define`
- [x] mutable and const `id`, `cpu`, and `pos`
- [x] mutable and const `GetArrayOfStructs` and `GetStructOfArrays`
- [x] `empty`, `size`, `numParticles`, `numRealParticles`,
  `numNeighborParticles`, and `numTotalParticles`
- [x] `setNumNeighbors` and `getNumNeighbors`
- [x] `resize` and instance `reserve`
- [x] both `push_back` overloads
- [x] all `push_back_real` overloads --
  [`issues/particle-tile-range-insert-end-dereference.md`](issues/particle-tile-range-insert-end-dereference.md)
- [x] all `push_back_int` overloads --
  [`issues/particle-tile-range-insert-end-dereference.md`](issues/particle-tile-range-insert-end-dereference.md)
- [x] all four component-count accessors
- [x] `shrink_to_fit`
- [x] `capacity`
- [x] `swap` --
  [`issues/particle-tile-swap-runtime-layout.md`](issues/particle-tile-swap-runtime-layout.md)
- [x] `getParticleTileData` and `getConstParticleTileData`
- [x] `collectVectors`
- [x] static batched `reserve`
- [x] `invalidateRuntimePtrCaches`
- [x] `refreshRuntimePtrCaches`
- [x] `refreshConstRuntimePtrCaches`

The `ConstSoAParticle` ID-generator declarations have no definitions in this
file. They were noted as declaration-only API, not counted as audited function
bodies; const particle handles are not expected to allocate IDs.

### `AMReX_WriteBinaryParticleData.H`

- [x] `KeepValidFilter::operator()`
- [x] `particle_detail::PSizeInFile`
- [x] `fillFlagsGpu`, `fillFlagsCpu`, and `fillFlags`
- [x] both `countFlagsGpu` overloads
- [x] both `countFlagsCpu` overloads
- [x] both `countFlags` dispatch overloads
- [x] `packParticleIDs`
- [x] `rPackParticleData`
- [x] `iPackParticleData`
- [x] `packIODataGpu`, `packIODataCpu`, and `packIOData`
- [x] `WriteBinaryParticleDataSync`
- [x] `WriteBinaryParticleDataAsync` and its submitted writer closure --
  [`issues/async-particle-output-untrimmed-tiles.md`](issues/async-particle-output-untrimmed-tiles.md),
  [`issues/async-particle-output-cross-level-offsets.md`](issues/async-particle-output-cross-level-offsets.md),
  [`issues/async-particle-output-advances-next-id.md`](issues/async-particle-output-advances-next-id.md)

### `AMReX_ParticleReduce.H`

- [x] both `particle_detail::call_f` overloads
- [x] all-level, single-level, and level-range `ReduceSum` overloads
- [x] all-level, single-level, and level-range `ReduceMax` overloads
- [x] all-level, single-level, and level-range `ReduceMin` overloads
- [x] all-level, single-level, and level-range `ReduceLogicalAnd` overloads
- [x] all-level, single-level, and level-range `ReduceLogicalOr` overloads
- [x] all-level, single-level, and level-range `ParticleReduce` overloads

No candidate bugs found. The CPU tuple-reduction path constructs one
`ReduceData` slot per OpenMP thread before the parallel loop, so concurrent
tile evaluations reduce into disjoint state as intended.

### `AMReX_NeighborList.H`

- [x] all nine `detail::call_check_pair` adapters
- [x] mutable `Neighbors::iterator` constructor, increment, comparison,
  dereference, and `index`
- [x] `Neighbors::const_iterator` constructor, increment, comparison,
  dereference, and `index`
- [x] mutable and const `Neighbors::begin`/`end`, plus `cbegin`/`cend`
- [x] `Neighbors::Neighbors`
- [x] `NeighborData::NeighborData`
- [x] `NeighborData::getNeighbors`
- [x] both constrained `isSame` overloads
- [x] all three `NeighborList::build` overloads --
  [`issues/neighbor-list-target-ghost-classification.md`](issues/neighbor-list-target-ghost-classification.md)
- [x] `NeighborList::data`
- [x] `NeighborList::numParticles` --
  [`issues/neighbor-list-empty-particle-count.md`](issues/neighbor-list-empty-particle-count.md)
- [x] const and mutable `GetOffsets`, `GetCounts`, and `GetList`
- [x] `NeighborList::print`

### `AMReX_ParticleCommunication.H` / `AMReX_ParticleCommunication.cpp`

- [x] `NeighborUnpackPolicy::resizeTiles`
- [x] `RedistributeUnpackPolicy::resizeTiles`
- [x] `ParticleCopyOp::clear`, `setNumLevels`, and `resize`
- [x] `ParticleCopyOp::numCopies` and `numLevels`
- [x] `ParticleCopyPlan::BuildWorkspace::BuildWorkspace`
- [x] `ParticleCopyPlan::forEachCopyBatch`
- [x] stable-ordered `ParticleCopyPlan::buildCopies`
- [x] OpenMP two-pass-host `ParticleCopyPlan::buildCopies`
- [x] atomic-scatter `ParticleCopyPlan::buildCopies`
- [x] `ParticleCopyPlan::finalizeBuildBoxCounts`
- [x] `ParticleCopyPlan::superParticleSize`
- [x] `ParticleCopyPlan::build` and `clear`
- [x] `ParticleCopyPlan::buildMPIStart` and `buildMPIFinish`
- [x] `ParticleCopyPlan::doHandShake`
- [x] `doHandShakeLocal`, `doHandShakeReduceScatter`,
  `doHandShakeOneSided`, and `doHandShakeAllToAll`
- [x] `GetSendBufferOffset::GetSendBufferOffset` and `operator()`
- [x] `packBuffer`
- [x] `unpackBuffer`
- [x] `communicateParticlesStart` and `communicateParticlesFinish`
- [x] `unpackRemotes`

No candidate bugs found. The OpenMP work partitions, GPU bucket counters,
per-rank alignment corrections, local-copy offsets, MPI metadata handshakes,
and remote source ordering were checked together across their conditional
paths.

### `AMReX_ParticleContainerBase.H` / `AMReX_ParticleContainerBase.cpp`

- [x] all `ParticleHandshakeWindow` special members and destructor
- [x] all `ParticleContainerBase` constructors, destructor, and copy/move
  declarations --
  [`issues/particle-container-base-anisotropic-ref-ratio.md`](issues/particle-container-base-anisotropic-ref-ratio.md)
- [x] all four `Define` overloads and `isDefined`
- [x] `reserveData`, `resizeData`, and `RedefineDummyMF` --
  [`issues/redefine-dummy-mf-empty-vector.md`](issues/redefine-dummy-mf-empty-vector.md)
- [x] all three `MakeMFIter` overloads
- [x] all three `SetParGDB` overloads
- [x] `SetParticleBoxArray`, `SetParticleDistributionMap`, and
  `SetParticleGeometry` --
  [`issues/redistribute-mask-stale-after-geometry-change.md`](issues/redistribute-mask-stale-after-geometry-change.md)
- [x] all hierarchy, geometry, level-count, and `ParGDB` accessors
- [x] verbosity and stable-redistribute getters/setters
- [x] `BufferMap`
- [x] `ensureParticleHandshakeWindow`, `particleHandshakeBuffer`, and
  `particleHandshakeWindow`
- [x] `NeighborProcs` and `OnSameGrids`
- [x] `arena` and `SetArena`
- [x] `CheckpointVersion`, `PlotfileVersion`, and `DataPrefix`
- [x] `MaxReaders`, `MaxParticlesPerRead`, `AggregationType`, and
  `AggregationBuffer`
- [x] `BuildRedistributeMask`
- [x] `defineBufferMap`

### `AMReX_ParticleUtil.H`

- [x] all six iterator/container `numParticlesOutOfRange` overloads
- [x] `getTileIndex` and its one-dimensional tiling closure
- [x] `numTilesInBox` and its one-dimensional tiling closure
- [x] `BinMapper::BinMapper` and `operator()`
- [x] `GetParticleBin::operator()`
- [x] all three `getParticleCell` overloads
- [x] `DefaultAssignor::operator()`
- [x] `getParticleGrid`
- [x] `enforcePeriodic`
- [x] both `partitionParticles` overloads
- [x] `removeInvalidParticles`
- [x] `partitionParticlesByDest`
- [x] `SameIteratorsOK`
- [x] `EnsureThreadSafeTiles` --
  [`issues/ensure-thread-safe-tiles-skips-empty.md`](issues/ensure-thread-safe-tiles-skips-empty.md)
- [x] `particle_detail::clearEmptyEntries`
- [x] both `PermutationForDeposition` overloads --
  [`issues/deposition-permutation-negative-refined-index.md`](issues/deposition-permutation-negative-refined-index.md)
- [x] `getDefaultCompNameReal` and `getDefaultCompNameInt`
- [x] classic and runtime-only `ReorderParticles`

The declarations for optional asynchronous-HDF5 wait hooks have no function
bodies in this file. `computeRefFac` and `computeNeighborProcs` are checked in
the `AMReX_ParticleUtil.cpp` section above.

### `AMReX_NeighborParticles.H`

- [x] `NeighborIndexMap::NeighborIndexMap` and stream insertion
- [x] both `NeighborCopyTag` constructors, comparisons, and stream insertion
- [x] `InverseCopyTag` stream insertion
- [x] `NeighborCommTag::NeighborCommTag`, comparisons, and stream insertion
- [x] all declared/defaulted/deleted neighbor-container special members
- [x] const and mutable `GetNeighbors`
- [x] `AddRealComp` and `AddIntComp` --
  [`issues/neighbor-container-add-component-default.md`](issues/neighbor-container-add-component-default.md)
- [x] `Redistribute` and `RedistributeLocal`
- [x] `setEnableInverse` and `enableInverse` --
  [`issues/neighbor-container-static-control-state.md`](issues/neighbor-container-static-control-state.md)
- [x] `numRealCommComps` and `numIntCommComps`
- [x] `NeighborTask::NeighborTask` and `operator<`
- [x] `hasNeighbors`

Out-of-line and template implementation bodies included from this header remain
listed separately below until their CPU and GPU branches are both complete.

### `AMReX_NeighborParticlesI.H`

- [x] all three neighbor-container constructors
- [x] `initializeCommComps`, `setRealCommComp`, `setIntCommComp`, and
  `calcCommSize` --
  [`issues/neighbor-communication-component-bounds.md`](issues/neighbor-communication-component-bounds.md)
- [x] all three `Regrid` overloads
- [x] `areMasksValid` and `BuildMasks` --
  [`issues/neighbor-container-static-control-state.md`](issues/neighbor-container-static-control-state.md),
  [`issues/redistribute-mask-stale-after-geometry-change.md`](issues/redistribute-mask-stale-after-geometry-change.md)
- [x] `GetNeighborCommTags`, `computeRefFac`, and `GetCommTagsBox`
- [x] `cacheNeighborInfo`
- [x] both `getNeighborTags` overloads --
  [`issues/neighbor-zero-cell-infinite-loop.md`](issues/neighbor-zero-cell-infinite-loop.md),
  [`issues/radius-neighbor-early-exit-misses-amr-boundaries.md`](issues/radius-neighbor-early-exit-misses-amr-boundaries.md)
- [x] both `fillNeighbors` overloads
- [x] `sumNeighbors`, `updateNeighbors`, and `clearNeighbors`
- [x] all five `buildNeighborList` overloads --
  [`issues/neighbor-list-nonpositive-bin-size.md`](issues/neighbor-list-nonpositive-bin-size.md),
  [`issues/ensure-thread-safe-tiles-skips-empty.md`](issues/ensure-thread-safe-tiles-skips-empty.md),
  [`issues/neighbor-list-target-ghost-classification.md`](issues/neighbor-list-target-ghost-classification.md)
- [x] `selectActualNeighbors` --
  [`issues/actual-neighbor-boundary-buffer-too-small.md`](issues/actual-neighbor-boundary-buffer-too-small.md)
- [x] `printNeighborList`
- [x] `resizeContainers`

### `AMReX_NeighborParticlesCPUImpl.H`

- [x] `detail::applyNeighborPeriodicShift`
- [x] `detail::shiftNeighborParticlePositions`
- [x] `detail::packNeighborParticleData` and
  `unpackNeighborParticleData`
- [x] `detail::getSummedRealComp` and `getSummedIntComp`
- [x] `detail::addSummedRealComp` and `addSummedIntComp`
- [x] `fillNeighborsCPU`
- [x] `sumNeighborsCPU` and `sumNeighborsMPI` --
  [`issues/sum-neighbors-component-range.md`](issues/sum-neighbors-component-range.md)
- [x] `updateNeighborsCPU`
- [x] `clearNeighborsCPU`
- [x] `getRcvCountsMPI`
- [x] `fillNeighborsMPI`

No other candidate bugs were found in the CPU-specific periodic packing,
inverse accumulation, cached-count update, or MPI unpack paths.

### `AMReX_NeighborParticlesGPUImpl.H`

- [x] `detail::forEachIntersectingTile`
- [x] `detail::getBoundaryBoxes`
- [x] `buildNeighborMask` --
  [`issues/gpu-neighbor-single-grid-tiling.md`](issues/gpu-neighbor-single-grid-tiling.md),
  [`issues/gpu-neighbor-fill-level-zero-only.md`](issues/gpu-neighbor-fill-level-zero-only.md),
  [`issues/redistribute-mask-stale-after-geometry-change.md`](issues/redistribute-mask-stale-after-geometry-change.md)
- [x] `buildNeighborCopyOp` --
  [`issues/gpu-neighbor-single-grid-tiling.md`](issues/gpu-neighbor-single-grid-tiling.md),
  [`issues/gpu-neighbor-fill-level-zero-only.md`](issues/gpu-neighbor-fill-level-zero-only.md)
- [x] `fillNeighborsGPU` --
  [`issues/gpu-neighbor-plan-uses-one-cell-rank-set.md`](issues/gpu-neighbor-plan-uses-one-cell-rank-set.md)
- [x] `updateNeighborsGPU` --
  [`issues/gpu-neighbor-plan-uses-one-cell-rank-set.md`](issues/gpu-neighbor-plan-uses-one-cell-rank-set.md)
- [x] `clearNeighborsGPU`

### `AMReX_ParticleContainer.H`

- [x] all five `ParticleContainer_impl` constructors and all special-member
  declarations
- [x] all four inline `Define` overloads
- [x] `numLocalTilesAtLevel`
- [x] checkpoint convenience overload
- [x] const and mutable, all-level and single-level `GetParticles`
- [x] const and mutable `ParticlesAt` grid/tile overloads
- [x] const and mutable iterator-based `ParticlesAt` overloads
- [x] both `DefineAndReturnParticleTile` overloads
- [x] level-directory, pre/post, unlink, cached-count, and
  `superParticleSize` accessors
- [x] named and unnamed `AddRealComp` overloads --
  [`issues/runtime-component-neighbor-only-tiles.md`](issues/runtime-component-neighbor-only-tiles.md)
- [x] named and unnamed `AddIntComp` overloads --
  [`issues/runtime-component-neighbor-only-tiles.md`](issues/runtime-component-neighbor-only-tiles.md)
- [x] all four runtime/total component-count accessors
- [x] `make_alike` and its `ContainerLike` result type --
  [`issues/make-alike-drops-cell-assignor.md`](issues/make-alike-drops-cell-assignor.md)
- [x] `GetRealSoANames` and `GetIntSoANames`
- [x] default `particlePostLocate` and `correctCellVectors` hooks

Declarations whose bodies are supplied by `AMReX_ParticleInit.H`,
`AMReX_ParticleContainerI.H`, or `AMReX_ParticleIO.H` are checked in their own
sections.

### `AMReX_ParticleContainerI.H`

- [x] `SetParticleSize`
- [x] `Initialize` --
  [`issues/particle-container-instance-io-options.md`](issues/particle-container-instance-io-options.md),
  [`issues/particle-tile-size-validation.md`](issues/particle-tile-size-validation.md)
- [x] `SetSoACompileTimeNames`
- [x] `HasRealComp`, `HasIntComp`, `GetRealCompIndex`, and
  `GetIntCompIndex`
- [x] `Index` and `Where`
- [x] `EnforcePeriodicWhere` --
  [`issues/periodic-where-ignores-cell-assignor.md`](issues/periodic-where-ignores-cell-assignor.md)
- [x] `PeriodicShift`, `Reset`, `reserveData`, `resizeData`, and
  `locateParticle`
- [x] `TotalNumberOfParticles`, `NumberOfParticlesInGrid`,
  `NumberOfParticlesAtLevel`, and `CapacityOfParticlesInGrid`
- [x] `ByteSpread` --
  [`issues/byte-spread-pure-soa-size.md`](issues/byte-spread-pure-soa-size.md),
  [`issues/lazy-particle-spread-return-value.md`](issues/lazy-particle-spread-return-value.md)
- [x] `PrintCapacity` --
  [`issues/lazy-particle-spread-return-value.md`](issues/lazy-particle-spread-return-value.md)
- [x] `ShrinkToFit`, `Increment`, and `IncrementWithTotal`
- [x] `RemoveParticlesAtLevel` --
  [`issues/particle-level-mutators-negative-level.md`](issues/particle-level-mutators-negative-level.md)
- [x] `RemoveParticlesNotAtFinestLevel` --
  [`issues/remove-particles-empty-hierarchy-underflow.md`](issues/remove-particles-empty-hierarchy-underflow.md)
- [x] `FilterVirt::FilterVirt` and `FilterVirt::operator()`
- [x] `TransformerVirt::operator()`
- [x] both `CreateVirtualParticles` overloads --
  [`issues/particle-level-mutators-negative-level.md`](issues/particle-level-mutators-negative-level.md),
  [`issues/cell-virtual-aggregation-soa-access.md`](issues/cell-virtual-aggregation-soa-access.md),
  [`issues/cell-virtual-aggregation-first-writer-race.md`](issues/cell-virtual-aggregation-first-writer-race.md),
  [`issues/cell-virtual-aggregation-global-sum.md`](issues/cell-virtual-aggregation-global-sum.md),
  [`issues/cell-virtual-aggregation-zero-weight.md`](issues/cell-virtual-aggregation-zero-weight.md),
  [`issues/ghost-virtual-runtime-component-layout.md`](issues/ghost-virtual-runtime-component-layout.md),
  [`issues/particle-aos-transfer-soa-components.md`](issues/particle-aos-transfer-soa-components.md)
- [x] `AssignGridFilter::AssignGridFilter` and
  `AssignGridFilter::operator()`
- [x] `TransformerGhost::operator()`
- [x] both `CreateGhostParticles` overloads --
  [`issues/particle-level-mutators-negative-level.md`](issues/particle-level-mutators-negative-level.md),
  [`issues/ghost-virtual-runtime-component-layout.md`](issues/ghost-virtual-runtime-component-layout.md),
  [`issues/particle-aos-transfer-soa-components.md`](issues/particle-aos-transfer-soa-components.md)
- [x] `clearParticles`
- [x] unfiltered `copyParticles` and `addParticles`
- [x] filtered `copyParticles` and `addParticles`
- [x] `Redistribute` and `Redistribute_impl`
- [x] `ReorderParticles`
- [x] `SortParticlesByCell`
- [x] `SortParticlesByBin` --
  [`issues/sort-particles-nonpositive-bin-size.md`](issues/sort-particles-nonpositive-bin-size.md)
- [x] `SortParticlesForDeposition`
- [x] `hostPartitionTile`
- [x] `ReserveForRedistribute` and `OK`
- [x] both `AddParticlesAtLevel` overloads --
  [`issues/particle-level-mutators-negative-level.md`](issues/particle-level-mutators-negative-level.md),
  [`issues/particle-aos-transfer-soa-components.md`](issues/particle-aos-transfer-soa-components.md)
- [x] `AssignCellDensitySingleLevel`
- [x] `ResizeRuntimeRealComp` and `ResizeRuntimeIntComp` --
  [`issues/runtime-component-neighbor-only-tiles.md`](issues/runtime-component-neighbor-only-tiles.md),
  [`issues/runtime-component-resize-name-desync.md`](issues/runtime-component-resize-name-desync.md),
  [`issues/runtime-component-negative-size.md`](issues/runtime-component-negative-size.md)

No additional candidate bugs were found in the counting, ordinary
redistribution, reordering, deposition, or filtered-copy paths.

### `AMReX_ParticleIO.H`

- [x] `WriteParticleRealData` and `ReadParticleRealData`
- [x] `FilterPositiveID::operator()`
- [x] both `Checkpoint` implementation overloads --
  [`issues/checkpoint-component-name-size.md`](issues/checkpoint-component-name-size.md),
  [`issues/particle-io-component-mask-size.md`](issues/particle-io-component-mask-size.md),
  [`issues/selective-checkpoint-cannot-restart.md`](issues/selective-checkpoint-cannot-restart.md)
- [x] all five unfiltered `WritePlotFile` implementation overloads --
  [`issues/particle-io-component-mask-size.md`](issues/particle-io-component-mask-size.md)
- [x] all five filtered `WritePlotFile` implementation overloads --
  [`issues/particle-io-component-mask-size.md`](issues/particle-io-component-mask-size.md),
  [`issues/async-particle-output-ignores-filter.md`](issues/async-particle-output-ignores-filter.md),
  [`issues/prepost-output-filter-count-mismatch.md`](issues/prepost-output-filter-count-mismatch.md)
- [x] `WriteBinaryParticleData` --
  [`issues/async-particle-output-ignores-filter.md`](issues/async-particle-output-ignores-filter.md),
  [`issues/async-output-prepost-incompatible.md`](issues/async-output-prepost-incompatible.md),
  [`issues/particle-io-ignores-parallel-context.md`](issues/particle-io-ignores-parallel-context.md)
- [x] `CheckpointPre` --
  [`issues/checkpoint-pre-device-and-pure-soa-count.md`](issues/checkpoint-pre-device-and-pure-soa-count.md),
  [`issues/prepost-output-filter-count-mismatch.md`](issues/prepost-output-filter-count-mismatch.md),
  [`issues/async-output-prepost-incompatible.md`](issues/async-output-prepost-incompatible.md),
  [`issues/particle-io-ignores-parallel-context.md`](issues/particle-io-ignores-parallel-context.md)
- [x] `CheckpointPost` --
  [`issues/async-output-prepost-incompatible.md`](issues/async-output-prepost-incompatible.md),
  [`issues/particle-io-ignores-parallel-context.md`](issues/particle-io-ignores-parallel-context.md)
- [x] `WritePlotFilePre` and `WritePlotFilePost`
- [x] `WriteParticles`
- [x] both `Restart` overloads --
  [`issues/selective-checkpoint-cannot-restart.md`](issues/selective-checkpoint-cannot-restart.md),
  [`issues/restart-appends-existing-particles.md`](issues/restart-appends-existing-particles.md),
  [`issues/particle-io-ignores-parallel-context.md`](issues/particle-io-ignores-parallel-context.md)
- [x] `ReadParticles`
- [x] `WriteAsciiFile` --
  [`issues/ascii-output-pure-soa-empty.md`](issues/ascii-output-pure-soa-empty.md),
  [`issues/particle-io-ignores-parallel-context.md`](issues/particle-io-ignores-parallel-context.md)

The data marshaling order, file-layout restart loop, same-precision real-data
dispatch, and ordinary synchronous per-grid writing paths yielded no other
candidate bugs.

### `AMReX_ParticleInit.H`

- [x] `InitFromAsciiFile` --
  [`issues/ascii-init-soa-data-never-staged.md`](issues/ascii-init-soa-data-never-staged.md),
  [`issues/ascii-init-runtime-component-overflow.md`](issues/ascii-init-runtime-component-overflow.md),
  [`issues/particle-init-negative-extradata.md`](issues/particle-init-negative-extradata.md),
  [`issues/ascii-init-nonpositive-replication.md`](issues/ascii-init-nonpositive-replication.md),
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/legacy-initializers-pure-soa-unavailable.md`](issues/legacy-initializers-pure-soa-unavailable.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitFromBinaryFile` --
  [`issues/particle-init-negative-extradata.md`](issues/particle-init-negative-extradata.md),
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/binary-init-exhausted-reader-assert.md`](issues/binary-init-exhausted-reader-assert.md),
  [`issues/binary-init-drops-soa-components.md`](issues/binary-init-drops-soa-components.md),
  [`issues/legacy-initializers-pure-soa-unavailable.md`](issues/legacy-initializers-pure-soa-unavailable.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitFromBinaryMetaFile` --
  [`issues/binary-metafile-stream-handling.md`](issues/binary-metafile-stream-handling.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitRandom` --
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/random-init-pure-soa-int-components.md`](issues/random-init-pure-soa-int-components.md),
  [`issues/random-init-pure-soa-consumes-extra-id.md`](issues/random-init-pure-soa-consumes-extra-id.md),
  [`issues/random-init-zero-volume-containing-box.md`](issues/random-init-zero-volume-containing-box.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitRandomPerBox` --
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/random-per-box-count-cubed.md`](issues/random-per-box-count-cubed.md),
  [`issues/amr-cell-initializers-skip-redistribution.md`](issues/amr-cell-initializers-skip-redistribution.md),
  [`issues/legacy-initializers-pure-soa-unavailable.md`](issues/legacy-initializers-pure-soa-unavailable.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitOnePerCell` --
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/one-per-cell-upper-offset.md`](issues/one-per-cell-upper-offset.md),
  [`issues/legacy-initializers-pure-soa-unavailable.md`](issues/legacy-initializers-pure-soa-unavailable.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)
- [x] `InitNRandomPerCell` --
  [`issues/particle-initializers-uninitialized-components.md`](issues/particle-initializers-uninitialized-components.md),
  [`issues/particle-initializers-bypass-tile-definition.md`](issues/particle-initializers-bypass-tile-definition.md),
  [`issues/amr-cell-initializers-skip-redistribution.md`](issues/amr-cell-initializers-skip-redistribution.md),
  [`issues/legacy-initializers-pure-soa-unavailable.md`](issues/legacy-initializers-pure-soa-unavailable.md),
  [`issues/particle-initialization-ignores-parallel-context.md`](issues/particle-initialization-ignores-parallel-context.md)

No additional candidate bugs were found in the ordinary AoS `ParticleInitData`
field assignments, valid-position location, or host-to-device copy ordering.

### `CMakeLists.txt` and `Make.package`

- [x] complete source/header inventory comparison --
  [`issues/cmake-omits-tracer-kernel-header.md`](issues/cmake-omits-tracer-kernel-header.md)
- [x] particle precision defines and include-directory wiring
- [x] C++ source parity between CMake and GNU Make
- [x] public-header parity apart from the recorded tracer-kernel omission

These files contain no C++ function bodies and do not change the function-like
definition count.

## Pending

None.

## Completion checks

- [x] all 46 `Src/Particle` entries reconciled: 44 code files and two build
  integration files
- [x] all 946 inventoried function-like definitions manually read and checked
- [x] all 94 candidate issue notes linked from an audited function or build
  integration check
- [x] every issue note contains a title, severity, affected code, explanation,
  and proposed patch
- [x] every checklist issue link resolves, with no unreferenced issue notes or
  duplicate issue titles
- [x] no unchecked checklist boxes, trailing whitespace, or conflict markers
