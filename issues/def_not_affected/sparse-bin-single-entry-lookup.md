# Sparse-bin lookup returns the only populated bin for every query

## Severity

Medium

## Affected code

`SparseBinIteratorFactory::getIndex` in `Src/Particle/AMReX_SparseBins.H`.

## Explanation

When the sparse structure contains exactly one populated bin, `getIndex`
returns index zero without comparing the requested bin number to
`m_bins_ptr[0]`:

```cpp
if (m_num_bins == 1) { return 0; }
```

Consequently, querying any empty bin produces an iterator over the one
populated bin's items. This violates the sparse lookup contract and can make a
caller process geometrically unrelated items. In particle-location code that
performs a second exact containment check this may be only a large performance
regression, but generic users of the public bin iterator can obtain incorrect
results directly.

## Proposed patch

Compare the stored bin number in the single-entry fast path:

```cpp
if (m_num_bins == 1) {
    return m_bins_ptr[0] == bin_number ? 0 : m_not_found;
}
```

Add a focused test that builds one nonzero bin and verifies that querying that
bin returns its items while queries immediately below and above it are empty.

## Verification

**Conclusion: Confirmed.**

`SparseBinIteratorFactory::getIndex()` returns index zero unconditionally when
`m_num_bins == 1`; it never compares the requested key with the sole stored
key. Any absent key is therefore reported as present.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This incorrect lookup is specific to `SparseBinIteratorFactory` and requires
a `SparseBins` instance containing exactly one stored key. Quokka does not
instantiate or call AMReX `SparseBins`; the binning used by AMReX's ordinary
particle-location machinery is the separate dense-bin implementation.

Quokka's use of particle containers does not implicitly select this sparse
factory. With no sparse-bin construction or lookup in its source or its active
particle path, Quokka cannot issue an absent-key query to the faulty
single-entry shortcut.
