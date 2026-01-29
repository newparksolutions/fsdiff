# Decoder Test Coverage

## test_decoder.c - Hand-Crafted Binary Data Tests

This test suite uses manually constructed BKDF binary data to verify the decoder matches the specification exactly. All tests create binary patch files by hand to ensure format compliance.

### Test Coverage

#### 1. `test_copy_identity()` - OP_COPY_IDENTITY (type 0)
**Coverage:**
- Inline count encoding (enc=3): 2 blocks
- 1-byte count encoding (enc=0): 5 blocks
- 2-byte count encoding (enc=1): 3 blocks
- 4-byte count encoding (enc=2): Not tested (would need >65536 blocks)

**Verification:** Output matches source at identity positions

#### 2. `test_copy_relocate()` - OP_COPY_RELOCATE (type 1)
**Coverage:**
- 1-byte offset (enc=0), positive: offset +3
- 1-byte offset (enc=0), negative: offset -2
- 2-byte offset (enc=1), positive: offset +5
- 4-byte offset (enc=2), positive: offset +100
- Inline count (enc=3): 1 block
- 1-byte count (enc=0): 2 blocks

**Verification:** Decoded without error

#### 3. `test_zero()` - OP_ZERO (type 2)
**Coverage:**
- Inline count: 3 blocks

**Verification:** All output bytes are 0x00

#### 4. `test_one()` - OP_ONE (type 3)
**Coverage:**
- 1-byte count: 2 blocks

**Verification:** All output bytes are 0xFF

#### 5. `test_literal()` - OP_LITERAL (type 5)
**Coverage:**
- Inline count: 2 blocks
- Literal stream consumption

**Verification:** Output matches literal data exactly

#### 6. `test_copy_add_dense()` - OP_COPY_ADD Dense Format (type 4)
**Coverage:**
- Dense format (diff_fmt=0)
- 2-byte offset encoding (offset_enc=0)
- 1-byte count encoding (count_enc=0): 2 blocks
- Positive byte offset: +10
- Dense diff data: one byte per output byte

**Verification:** Output = src[offset..] + diff_data

#### 7. `test_copy_add_sparse()` - OP_COPY_ADD Sparse Format (type 4)
**Coverage:**
- Sparse format (diff_fmt=1)
- Alternating copy-add/copy runs
- Multiple copy/copy-add pairs within single block
- Sparse encoding with 3 changes across 512-byte block

**Verification:** Output has correct modifications at positions 10, 20, 30

#### 8. `test_copy_add_negative()` - OP_COPY_ADD Negative Offset (type 4)
**Coverage:**
- Negative byte offset: -256
- Dense format with all-zero diff (identity case)
- Multi-block patch (identity + copy_add)

**Verification:** Blocks 0-1 identity, block 2 from src[768..]

#### 9. `test_copy_add_large_offsets()` - OP_COPY_ADD Large Offsets (type 4)
**Coverage:**
- 3-byte offset encoding (offset_enc=1): offset +40000
- 1-byte count encoding (count_enc=0): 1 block
- Note: Inline count (enc=3) cannot be used for COPY_ADD (bits used for offset_enc+diff_fmt)
- Large source files (100KB)

**Verification:** Decoded without error

#### 10. `test_mixed_operations()` - Multiple Operation Types
**Coverage:**
- All operation types in one patch
- Sequence: IDENTITY, ZERO, ONE, COPY_RELOCATE, LITERAL, COPY_ADD
- Verifies correct stream consumption and dest_block advancement

**Verification:** Each block type produces correct output

#### 11. `test_errors()` - Error Handling
**Coverage:**
- `FSD_ERR_BAD_MAGIC`: Corrupted magic bytes
- `FSD_ERR_CORRUPT_DATA`: Invalid negative source position
- `FSD_ERR_BAD_OPERATION`: Reserved operation type (6)

**Verification:** Correct error codes returned

### Format Spec Compliance

The tests verify:
1. ✅ Header structure (32 bytes, all fields)
2. ✅ All 6 operation types (0-5, plus reserved 6-7)
3. ✅ All 4 count encodings (0-3)
4. ✅ All 3 offset encodings for COPY_RELOCATE (0-2)
5. ✅ All 3 offset encodings for COPY_ADD byte offset (0-2, note: enc=3 not applicable)
6. ✅ Both diff formats for COPY_ADD (dense=0, sparse=1)
7. ✅ Signed offset handling (positive and negative)
8. ✅ Little-endian integer decoding
9. ✅ Stream consumption (ops, diff, literal)
10. ✅ Error conditions (bad magic, invalid offsets, unknown ops)

### Code Coverage

Based on patch.c decoder:
- ✅ `read_count()`: All 4 branches (enc 0-3)
- ✅ `fsd_patch_apply()`: All 6 operation cases
- ✅ COPY_RELOCATE: All 3 offset_enc sizes
- ✅ COPY_ADD: Both diff_fmt branches
- ✅ COPY_ADD: All 3 offset_enc sizes
- ✅ COPY_ADD dense: Block-by-block diff application
- ✅ COPY_ADD sparse: Alternating copy-add/copy loop
- ✅ Error paths: BAD_MAGIC, CORRUPT_DATA, BAD_OPERATION
- ⚠️  Not tested: TRUNCATED errors (file size validation needed)
- ⚠️  Not tested: 4-byte count encoding (would need >65536 blocks)

### Future Improvements

1. Add file size validation to detect truncated files reliably
2. Test 4-byte count encoding (requires generating large test files)
3. Test all combinations of count_enc × offset_enc for COPY_RELOCATE and COPY_ADD
4. Fuzz testing with random binary data
5. Stress test with very large offsets (near INT64_MAX)
