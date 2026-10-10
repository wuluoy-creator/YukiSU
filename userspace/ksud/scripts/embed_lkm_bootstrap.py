#!/usr/bin/env python3

import struct
import sys
from pathlib import Path


ELF64_HEADER_SIZE = 64
ELF64_SECTION_SIZE = 64
ELF64_SYMBOL_SIZE = 24
ELF64_RELA_SIZE = 24
ET_REL = 1
EM_AARCH64 = 183
SHT_REL = 9
SHT_RELA = 4
SHT_SYMTAB = 2
SHT_STRTAB = 3
SUPPORTED_RELOCATIONS = {257, 258, 275, 277, 282, 283}

ENTRY_SYMBOLS = {
    "ksu_bootstrap",
    "ksu_memblock_reserve_wrapper",
    "ksu_strndup_user_adapter",
}

REQUIRED_DEFINITIONS = {
    "ksu_ext_async_synchronize_full",
    "ksu_ext_kimage_voffset",
    "ksu_ext_memstart_addr",
    "ksu_ext_vmalloc",
    "ksu_ext_memcpy",
    "ksu_ext_load_module",
    "ksu_ext_memblock_reserve",
    "ksu_ext_strndup_user",
    "ksu_ext_kstrdup",
    "ksu_image_base",
    "ksu_capsule_image_offset",
    "ksu_page_offset",
    "ksu_capsule_magic",
    "ksu_capsule_version",
    "ksu_capsule_header_size",
    "ksu_capsule_size",
    "ksu_module_capsule_offset",
    "ksu_module_size",
    "ksu_fixup_capsule_offset",
    "ksu_fixup_count",
    "ksu_load_info_size",
    "ksu_load_info_hdr_offset",
    "ksu_load_info_len_offset",
    "ksu_reserve_extension",
    "ksu_gfp_kernel",
}


def checked_slice(data: bytes, offset: int, size: int, description: str) -> bytes:
    end = offset + size
    if offset < 0 or size < 0 or end > len(data):
        raise ValueError(f"{description} is outside the object")
    return data[offset:end]


def read_c_string(data: bytes, offset: int, description: str) -> str:
    if offset < 0 or offset >= len(data):
        raise ValueError(f"{description} has an invalid string offset")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"{description} is not null-terminated")
    return data[offset:end].decode("ascii")


def validate_object(data: bytes) -> None:
    if len(data) < ELF64_HEADER_SIZE or data[:4] != b"\x7fELF":
        raise ValueError("bootstrap object is not an ELF file")
    if data[4:7] != b"\x02\x01\x01":
        raise ValueError("bootstrap object must be little-endian ELF64")
    if struct.unpack_from("<H", data, 16)[0] != ET_REL:
        raise ValueError("bootstrap object must be ET_REL")
    if struct.unpack_from("<H", data, 18)[0] != EM_AARCH64:
        raise ValueError("bootstrap object must target AArch64")
    if struct.unpack_from("<I", data, 20)[0] != 1:
        raise ValueError("bootstrap object uses an unsupported ELF version")

    section_offset = struct.unpack_from("<Q", data, 40)[0]
    header_size = struct.unpack_from("<H", data, 52)[0]
    program_count = struct.unpack_from("<H", data, 56)[0]
    section_entry_size = struct.unpack_from("<H", data, 58)[0]
    section_count = struct.unpack_from("<H", data, 60)[0]
    section_names_index = struct.unpack_from("<H", data, 62)[0]
    if (
        header_size < ELF64_HEADER_SIZE
        or program_count != 0
        or section_entry_size != ELF64_SECTION_SIZE
        or section_count == 0
        or section_names_index >= section_count
    ):
        raise ValueError("bootstrap object has malformed ELF tables")

    section_table = checked_slice(
        data,
        section_offset,
        section_count * ELF64_SECTION_SIZE,
        "section table",
    )
    sections = []
    for index in range(section_count):
        fields = struct.unpack_from("<IIQQQQIIQQ", section_table, index * ELF64_SECTION_SIZE)
        sections.append(
            {
                "name_offset": fields[0],
                "type": fields[1],
                "offset": fields[4],
                "size": fields[5],
                "link": fields[6],
                "info": fields[7],
                "entry_size": fields[9],
            }
        )

    section_names = sections[section_names_index]
    if section_names["type"] != SHT_STRTAB:
        raise ValueError("section-name table is not a string table")
    names = checked_slice(
        data, section_names["offset"], section_names["size"], "section-name table"
    )
    for section in sections:
        section["name"] = read_c_string(names, section["name_offset"], "section name")
        if section["type"] != 8:
            checked_slice(data, section["offset"], section["size"], section["name"])

    for required_section in (".text.ksu_bootstrap", ".rodata.ksu_bootstrap"):
        if sum(section["name"] == required_section for section in sections) != 1:
            raise ValueError(f"bootstrap must contain exactly one {required_section} section")

    symbol_tables = [section for section in sections if section["type"] == SHT_SYMTAB]
    if len(symbol_tables) != 1:
        raise ValueError("bootstrap must contain exactly one symbol table")
    symbols = symbol_tables[0]
    if symbols["entry_size"] != ELF64_SYMBOL_SIZE or symbols["size"] % ELF64_SYMBOL_SIZE:
        raise ValueError("bootstrap symbol table is malformed")
    if symbols["link"] >= len(sections):
        raise ValueError("bootstrap symbol table has an invalid string table")
    symbol_names_section = sections[symbols["link"]]
    if symbol_names_section["type"] != SHT_STRTAB:
        raise ValueError("bootstrap symbol names are not in a string table")
    symbol_names = checked_slice(
        data,
        symbol_names_section["offset"],
        symbol_names_section["size"],
        "symbol-name table",
    )

    defined = set()
    undefined = set()
    symbol_count = symbols["size"] // ELF64_SYMBOL_SIZE
    for index in range(symbol_count):
        offset = symbols["offset"] + index * ELF64_SYMBOL_SIZE
        name_offset, _, _, section_index, _, _ = struct.unpack_from("<IBBHQQ", data, offset)
        name = read_c_string(symbol_names, name_offset, "symbol name")
        if not name:
            continue
        if section_index == 0:
            undefined.add(name)
        else:
            defined.add(name)

    missing_entries = ENTRY_SYMBOLS - defined
    if missing_entries:
        raise ValueError(f"bootstrap entry symbols are missing: {sorted(missing_entries)}")
    missing_definitions = REQUIRED_DEFINITIONS - undefined
    if missing_definitions:
        raise ValueError(
            f"bootstrap relocation definitions are missing: {sorted(missing_definitions)}"
        )

    relocation_count = 0
    for section in sections:
        if section["type"] == SHT_REL:
            raise ValueError("bootstrap uses unsupported REL relocations")
        if section["type"] != SHT_RELA:
            continue
        if section["entry_size"] != ELF64_RELA_SIZE or section["size"] % ELF64_RELA_SIZE:
            raise ValueError(f"relocation section {section['name']} is malformed")
        relocation_count += section["size"] // ELF64_RELA_SIZE
        for index in range(section["size"] // ELF64_RELA_SIZE):
            offset = section["offset"] + index * ELF64_RELA_SIZE
            _, info, _ = struct.unpack_from("<QQq", data, offset)
            relocation_type = info & 0xFFFFFFFF
            if relocation_type not in SUPPORTED_RELOCATIONS:
                raise ValueError(f"unsupported AArch64 relocation {relocation_type}")
    if relocation_count == 0:
        raise ValueError("bootstrap object contains no relocations")


def generate_source(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 12):
        chunk = ", ".join(f"0x{value:02x}" for value in data[offset : offset + 12])
        rows.append(f"    {chunk},")
    byte_rows = "\n".join(rows)
    return f'''// Auto-generated file. Do not edit.

#include "boot/lkm_image_bootstrap.hpp"

namespace ksud::boot::lkm_image {{
namespace {{

alignas(8) constexpr uint8_t kBootstrapObject[] = {{
{byte_rows}
}};

}}  // namespace

BootstrapObjectView bootstrap_object() noexcept {{
    return {{kBootstrapObject, sizeof(kBootstrapObject)}};
}}

}}  // namespace ksud::boot::lkm_image
'''


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bootstrap.o> <output.cpp>", file=sys.stderr)
        return 2

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    try:
        data = input_path.read_bytes()
        validate_object(data)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8", newline="\n") as output:
            output.write(generate_source(data))
    except (OSError, UnicodeDecodeError, ValueError, struct.error) as error:
        print(f"Cannot embed {input_path}: {error}", file=sys.stderr)
        return 1

    print(f"Embedded {input_path} ({len(data)} bytes) into {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
