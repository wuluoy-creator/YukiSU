# Ramdisk editor protocol

The manager's ramdisk editor communicates with a long-lived ksud process over binary stdin/stdout frames:

```text
ksud ramdisk-editor <ramdisk.cpio>
ksud boot-ramdisk-editor <source.img> <output.img>
```

Both commands keep CPIO documents in memory. Changes become persistent only after a successful `DUMP`. `CLOSE` and EOF discard changes made since the last dump. Raw mode atomically replaces the input CPIO; boot mode atomically creates or replaces the output image. Using the source path as the output path replaces that source. These commands do not flash a partition. Diagnostics use stderr; the original stdout is reserved for protocol frames.

The implementation is [ramdisk_editor.cpp](../userspace/ksud/src/boot/ramdisk_editor.cpp), backed by [CpioDocument](../userspace/ksud/third_party/MagiskbootAlone/src/cpio.hpp) and [BootRamdiskDocument](../userspace/ksud/third_party/MagiskbootAlone/src/boot_ramdisk.cpp). This is independent of the SUMHP daemon protocol and module mount backends.

## Supported images and limits

Boot mode accepts plain AOSP `boot`, `init_boot` and `vendor_boot` images with header version 3 or 4 and a nonempty ramdisk. Vendor v4 ramdisk-table entries are exposed as separate documents. Legacy/vendor-wrapped layouts, AVB1-signed layouts and other layouts rejected by `BootRamdiskDocument` are unsupported.

Each fragment retains its compression: uncompressed, gzip/zopfli, xz, lz4, lz4 legacy or lz4 LG. Rebuilding retains supported AVB footer/vbmeta layout and adjusts offsets; it does **not** regenerate a valid cryptographic signature for modified contents. An image that no longer fits before the existing AVB footer is rejected. No unpacked `kernel` or `ramdisk.cpio` workspace files are required.

Current source limits include 512 MiB CPIO content, 262,144 entries per document, 65,536-byte CPIO path limit, 16 MiB control/list bodies, 528 MiB maximum request payload and 256 MiB boot-image output. Boot loading also bounds combined decompressed fragments. Clients must read `HELLO` limits and capabilities instead of treating these numbers as a permanent contract.

## Frame format

All wire integers are unsigned little-endian. Each frame starts with exactly 20 bytes:

| Field | Type | Value |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `YRCP` |
| Protocol version | `u16` | `3` |
| Opcode | `u16` | Request opcode; responses set `0x8000` |
| Request ID | `u32` | Chosen by client and echoed by server |
| Payload size | `u64` | Bytes after the header |

Every response payload begins with a `u32` status. The successful response bodies below exclude those four status bytes.

| Status | Meaning |
| ---: | --- |
| 0 | OK |
| 1 | Invalid request |
| 2 | Node not found |
| 3 | Operation failed |
| 4 | I/O error |
| 5 | Limit exceeded |
| 6 | Unsupported protocol version |

Strings are a `u32` byte length followed by that many bytes, with no terminator. The Android client uses UTF-8. Optional strings use length `0xffffffff` for absence. The server does not return a textual error field; capture stderr separately.

Send the entire request payload before awaiting its response. `REPLACE` and `CREATE_FILE` stream the bytes remaining in their frame as content. Fixed-layout requests reject trailing bytes. `READ` returns at most `min(length, size - offset)` bytes and rejects offsets beyond EOF.

Malformed/truncated headers terminate the session. Unsupported version, a request with the response bit set, or an oversized frame sends the corresponding error when possible and then terminates. Ordinary rejected operations can leave the session usable after their remaining payload is drained. An incomplete response must be treated as a transport failure even if its status prefix was OK.

## Node IDs and records

Wire IDs encode `(document_index + 1)` in the high 16 bits and the local node ID in the low 48 bits. Each document's local root is zero; **wire zero is not a root**. Use the IDs returned by `HELLO` or `LIST_RAMDISKS`. IDs are valid only for this process and remain stable across moves/renames of an existing node.

`STAT` and `LIST` serialize each node as:

```text
u64 id
u64 parent_id
u64 size
u32 inode
u32 mode
u32 uid
u32 gid
u32 nlink
u32 mtime_seconds
u32 dev_major
u32 dev_minor
u32 rdev_major
u32 rdev_minor
u8  synthetic_directory
u8  content_kind
string name
string normalized_path
optional-string symbolic_link_target
```

`mode` contains the file type and permission bits. Synthetic directories represent missing parent directory entries in the source archive. Content kind `0` means unknown, `1` means ELF. Detection uses ELF magic, so a truncated ELF can be classified as ELF while its `ELF_HEADER` request fails.

## Operations

| Opcode | Name | Request payload | Successful response body |
| ---: | --- | --- | --- |
| 1 | HELLO | Empty | `u32 version`, `u64 first_root_id`, `u64 max_content`, `u64 max_entries`, `u32 capabilities`, `u8 dirty` |
| 2 | STAT | `u64 id` | Node record |
| 3 | LIST | `u64 directory_id` | `u32 count`, node records |
| 4 | READ | `u64 id`, `u64 offset`, `u64 length` | Raw bytes |
| 5 | REPLACE | `u64 id`, raw content | Empty |
| 6 | CREATE_FILE | `u64 parent`, `u32 permissions`, `u32 uid`, `u32 gid`, `string name`, raw content | `u64 created_id` |
| 7 | CREATE_DIRECTORY | `u64 parent`, `u32 permissions`, `u32 uid`, `u32 gid`, `string name` | `u64 created_id` |
| 8 | CREATE_SYMBOLIC_LINK | `u64 parent`, `u32 uid`, `u32 gid`, `string name`, `string target` | `u64 created_id` |
| 9 | CREATE_HARD_LINK | `u64 parent`, `u64 target_id`, `string name` | `u64 created_id` |
| 10 | COPY | `u64 id`, `u64 destination_directory`, `string new_name` | `u64 created_id` |
| 11 | MOVE | `u64 id`, `u64 destination_directory`, `string new_name` | Empty |
| 12 | REMOVE | `u64 id`, `u8 recursive` | Empty |
| 13 | UPDATE_METADATA | `u64 id`, `u32 mask`, selected `u32` values | Empty |
| 14 | DUMP | Empty | Empty |
| 15 | CLOSE | Empty | Empty, then process exits |
| 16 | LIST_RAMDISKS | Empty | `u32 count`, fragment records |
| 17 | ELF_HEADER | `u64 id` | Structured ELF header record |

Hard links, copies and moves require source/target IDs from the same fragment. `recursive` must be `0` or `1`. Metadata mask values are `0x1` permissions, `0x2` UID, `0x4` GID and `0x8` mtime; selected values appear in that order. A zero mask or unknown bits is invalid. Mutation failure is reported as `OPERATION_FAILED`, including unresolved mutation targets; do not assume every missing node produces `NOT_FOUND`.

A successful mutation sets the session dirty flag. Successful `DUMP` clears it and keeps the session open; failed `DUMP` leaves the flag unchanged. There is no automatic save on close.

### Capabilities

`HELLO` currently advertises all of the following bits:

| Bit | Capability | Bit | Capability |
| ---: | --- | ---: | --- |
| 0 | Read content | 8 | Remove |
| 1 | Replace content | 9 | Update metadata |
| 2 | Create regular file | 10 | Atomic dump |
| 3 | Create directory | 11 | Ranged read |
| 4 | Create symbolic link | 12 | Implicit directories |
| 5 | Create hard link | 13 | Multiple fragments |
| 6 | Copy | 14 | Content types |
| 7 | Move / rename | 15 | Structured ELF header |

### Ramdisk fragment record

Each `LIST_RAMDISKS` entry is serialized as:

```text
u32 index
u64 root_id
u64 packed_size
u32 vendor_type
u8  is_vendor
string name
string compression
u32 board_id[16]
```

Fragment metadata describes the loaded image; `packed_size` is not recalculated after edits. Raw CPIO mode returns one fragment named `ramdisk`, packed size zero, non-vendor, compression `none`. Boot mode returns original fragment names, vendor types and board IDs. `HELLO` returns only the first root, so use `LIST_RAMDISKS` to discover all fragments.

### ELF header record

`ELF_HEADER` requires a regular file with ELF magic. It reads up to 64 header bytes and calls the structured `readelf_toyboxAlone` API, rather than parsing CLI output. Version 1 has a fixed 96-byte body:

```text
u16 schema_version = 1
u16 fixed_size = 96
u32 readelf_api_version
u32 header_flags
u32 reserved
u64 file_size
u8  ident[16]
u8  elf_class
u8  data_encoding
u8  ident_version
u8  os_abi
u8  abi_version
u8  reserved[3]
u16 type
u16 machine
u32 elf_version
u64 entry
u64 program_header_offset
u64 section_header_offset
u32 flags
u16 header_size
u16 program_header_entry_size
u16 program_header_count
u16 section_header_entry_size
u16 section_header_count
u16 section_name_index
```

All wire values remain little-endian even for a big-endian ELF. Header flag bits `0`, `1` and `2` identify extended program-header count, section-header count and section-name index encodings. This operation inspects the header; it does not return complete program/section tables or guarantee that the executable is loadable.

A minimal client flow is: start the process, send `HELLO`, discover roots with `LIST_RAMDISKS`, issue `LIST`/`STAT` and edits, explicitly `DUMP`, then `CLOSE`. Check both every response status and process exit status.
