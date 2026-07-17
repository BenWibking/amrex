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

