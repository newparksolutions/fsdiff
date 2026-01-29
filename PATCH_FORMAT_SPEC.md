# Block Diff Patch Format Specification

**Version**: 1.0  
**Magic**: `BKDF`

## Overview

This format describes binary patches for block-oriented storage images (filesystems, disk images). It is optimised for:

- Large images (multi-GiB)
- High block-level similarity between source and target
- Partial block matches with byte-level offsets
- External compression (designed to compress well with xz/lzma)

## File Structure

```
┌─────────────────────────────┐
│ Header (16 bytes)           │
├─────────────────────────────┤
│ Operation Stream            │
│ (variable length)           │
├─────────────────────────────┤
│ Diff Stream                 │
│ (variable length)           │
├─────────────────────────────┤
│ Literal Stream              │
│ (variable length)           │
└─────────────────────────────┘
```

The file is uncompressed. Users should apply external compression (e.g., xz) as appropriate.

## Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | magic | `"BKDF"` (0x42 0x4B 0x44 0x46) |
| 4 | 1 | version | Format version (1) |
| 5 | 1 | block_size_log2 | Block size as power of 2 (e.g., 12 = 4096) |
| 6 | 2 | reserved | Must be zero |
| 8 | 8 | dest_blocks | Total blocks in destination image (LE) |
| 16 | 8 | op_stream_len | Bytes in operation stream (LE) |
| 24 | 8 | diff_stream_len | Bytes in diff stream (LE) |

```c
struct bkdf_header {
    uint8_t  magic[4];        // "BKDF"
    uint8_t  version;         // 1
    uint8_t  block_size_log2; // e.g., 12 for 4096
    uint16_t reserved;        // Must be 0
    uint64_t dest_blocks;     // Total blocks in destination
    uint64_t op_stream_len;   // Bytes in operation stream
    uint64_t diff_stream_len; // Bytes in diff stream
} __attribute__((packed));    // Total: 32 bytes
```

## Stream Layout

Following the 32-byte header, the three streams are concatenated:

```
[Header: 32 bytes]
[Operation Stream: op_stream_len bytes]
[Diff Stream: diff_stream_len bytes]
[Literal Stream: to end of file]
```

The literal stream length is implicit:
```
literal_stream_len = file_size - 32 - op_stream_len - diff_stream_len
```

## Operation Stream

Operations are executed sequentially. A destination block pointer starts at 0 and advances implicitly.

### Operation Encoding

First byte format:
```
Bits 7-5: Operation type (0-7)
Bits 4-3: Count encoding
Bits 2-0: Operation-specific flags
```

Count encoding:
| Value | Meaning |
|-------|---------|
| 0 | Count follows as 1 byte (1-256, stored as 0-255) |
| 1 | Count follows as 2 bytes little-endian (1-65536) |
| 2 | Count follows as 4 bytes little-endian |
| 3 | Count is bits[2:0] + 1 (inline, range 1-8) |

### Operations

#### OP_COPY_IDENTITY (0)

Copy blocks from reference at the same position as destination.

```
[0:3][count_enc:2][000:3] [count]
```

- Advances destination pointer by `count` blocks
- Source position equals destination position

#### OP_COPY_RELOCATE (1)

Copy blocks from reference at a different position.

```
[1:3][count_enc:2][offset_enc:2][sign:1] [count] [offset]
```

- `count_enc`: Count encoding (see above), but inline (3) is not used
- `offset_enc`: 0=1 byte, 1=2 bytes, 2=4 bytes
- `sign`: 0=positive, 1=negative
- `offset`: Unsigned block offset

Source position (blocks) = destination position + (sign ? -offset : +offset)

#### OP_ZERO (2)

Write blocks filled with 0x00.

```
[2:3][count_enc:2][000:3] [count]
```

#### OP_ONE (3)

Write blocks filled with 0xFF.

```
[3:3][count_enc:2][000:3] [count]
```

#### OP_COPY_ADD (4)

Copy bytes from reference with a byte offset, adding differences.

```
[4:3][count_enc:2][offset_enc:2][diff_fmt:1] [byte_offset] [count] [diff_len:4]
```

- `offset_enc`: Byte offset size (0=2, 1=3, 2=4 bytes)
- `diff_fmt`: Diff stream format (0=dense, 1=sparse)
- `byte_offset`: Signed byte offset (two's complement)
- `count`: Number of blocks
- `diff_len`: Bytes to consume from diff stream (4 bytes, little-endian)

Source byte position = (destination block × block_size) + byte_offset

**Diff format 0 (dense)**: `diff_len` bytes from diff stream, one per output byte.
Each output byte = source byte + diff byte (mod 256).

**Diff format 1 (sparse)**: Alternating copy/copy-add encoding, processed per block.

The format alternates between two modes, starting in **copy-add mode**:

```
┌─────────────────────────────────────────────────────────┐
│ For each block, repeat until block_size bytes emitted: │
├─────────────────────────────────────────────────────────┤
│ Copy-add: [len:varint] [diff_values:len]               │
│   - Read len (base-128 varint) from diff stream        │
│   - Read len diff bytes                                │
│   - For each: output = source + diff_value (mod 256)   │
│   - Advances source and output pointers by len         │
├─────────────────────────────────────────────────────────┤
│ Copy: [len:varint]                                     │
│   - Read len (base-128 varint) from diff stream        │
│   - Copy len bytes from source to output unchanged     │
│   - Advances source and output pointers by len         │
└─────────────────────────────────────────────────────────┘
```

**Base-128 varint encoding**: Each byte uses bit 7 as continuation flag (1=more bytes follow, 0=last byte) and bits 6-0 for data. Examples:
- 127 → `0x7F` (1 byte)
- 128 → `0x80 0x01` (2 bytes)
- 256 → `0x80 0x02` (2 bytes)
- 1000 → `0xE8 0x07` (2 bytes)
- 4096 → `0x80 0x20` (2 bytes)

The alternation continues until the cumulative byte count reaches `block_size`. The final operation may be truncated if the count would exceed `block_size`.

**Optimization**: Copy-add runs terminate only when encountering two consecutive zero diff bytes. Single zero bytes surrounded by non-zeros are included in the copy-add run (encoded as diff=0) to avoid one-byte copy operations.

Example: A 4096-byte block with differences at positions 100, 150, and 200:

```
Copy-add: len=0           # No changes at start (0x00)
Copy:     len=100         # Copy bytes 0-99 unchanged (0x64)
Copy-add: len=1, diff=d1  # Byte 100: output = src + d1 (0x01 d1)
Copy:     len=49          # Copy bytes 101-149 unchanged (0x31)
Copy-add: len=1, diff=d2  # Byte 150: output = src + d2 (0x01 d2)
Copy:     len=49          # Copy bytes 151-199 unchanged (0x31)
Copy-add: len=1, diff=d3  # Byte 200: output = src + d3 (0x01 d3)
Copy:     len=3895        # Copy bytes 201-4095 unchanged (0xB7 0x1E = 3895 in varint)
```

For blocks with many changes (~500 per block average), this format avoids storing 2-byte positions for each change, relying instead on the implicit position tracking. Base-128 encoding allows efficient representation of long unchanged runs without artificial 255-byte limits.

#### OP_LITERAL (5)

Write raw bytes from literal stream.

```
[5:3][count_enc:2][000:3] [count]
```

Consumes `count × block_size` bytes from literal stream.

#### Reserved (6-7)

Reserved for future use. Decoders must reject files containing these operations.

## Applying a Patch

```python
def apply_patch(reference, patch, output):
    header = read_header(patch)
    block_size = 1 << header.block_size_log2
    
    dest_pos = 0  # Current destination block
    
    while dest_pos < header.dest_blocks:
        op = read_operation(patch.op_stream)
        
        if op.type == OP_COPY_IDENTITY:
            src_offset = dest_pos * block_size
            copy(reference, src_offset, output, op.count * block_size)
            dest_pos += op.count
            
        elif op.type == OP_COPY_RELOCATE:
            src_block = dest_pos + op.signed_offset
            copy(reference, src_block * block_size, output, op.count * block_size)
            dest_pos += op.count
            
        elif op.type == OP_ZERO:
            write_zeros(output, op.count * block_size)
            dest_pos += op.count
            
        elif op.type == OP_ONE:
            write_ones(output, op.count * block_size)
            dest_pos += op.count
            
        elif op.type == OP_COPY_ADD:
            src_byte = dest_pos * block_size + op.byte_offset
            diff_data = read(patch.diff_stream, op.diff_len)
            apply_diff(reference, src_byte, diff_data, op.diff_fmt, 
                       output, op.count * block_size)
            dest_pos += op.count
            
        elif op.type == OP_LITERAL:
            literal = read(patch.literal_stream, op.count * block_size)
            write(output, literal)
            dest_pos += op.count
```

## Design Rationale

**Block-based operations**: COPY_IDENTITY and COPY_RELOCATE use block granularity because analysis shows relocations are almost always block-aligned. This saves ~12 bits per operation.

**Byte-based COPY_ADD**: Partial matches often have sub-block byte offsets due to inserted/deleted content within files.

**Per-operation diff format**: Allows optimal encoding per region. Dense works well for high-entropy or heavily-changed blocks; sparse for blocks with localised modifications.

**Sparse format design**: The alternating copy/copy-add scheme avoids storing 2-byte positions for each change. For blocks with ~500 changes (average ~8 bytes between changes), this is more compact than position-value pairs and compresses well due to the regular structure.

**64-bit stream lengths**: Supports patch files for very large images (multi-TiB) without artificial limits.

**No internal compression**: Separating compression from the format allows users to choose appropriate algorithms and settings. The stream separation (ops, diffs, literals) is designed to compress well — similar data is grouped together.

**Relative offsets**: Most relocations are local, so relative offsets typically fit in 1-2 bytes rather than requiring full 4-byte addresses.

## Recommended Usage

1. Generate uncompressed patch file
2. Compress with `xz -9` or similar for distribution
3. Decompress before applying, or use streaming decompression

For embedded targets with limited memory, the patch can be applied with streaming decompression since operations are sequential and streams are consumed in order.
