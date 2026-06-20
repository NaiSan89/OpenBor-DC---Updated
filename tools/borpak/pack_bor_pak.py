#!/usr/bin/env python3
"""Build an OpenBOR PAK archive from a data directory."""

from __future__ import annotations

import argparse
import os
import stat
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


MAGIC = b"PACK"
VERSION = 0
UINT32_MAX = 0xFFFFFFFF
UINT32 = struct.Struct("<I")
INDEX_ENTRY = struct.Struct("<III")
COPY_BUFFER_SIZE = 1024 * 1024
MAX_ARCHIVED_NAME_SIZE = 80  # Includes NUL; matches packfile.h's namebuf[80].


class PakError(Exception):
    """Raised when a data directory cannot be converted to a valid PAK."""


@dataclass(frozen=True)
class SourceEntry:
    source: Path
    archived_name: bytes
    size: int


def collect_files(data_dir: Path, output: Path) -> list[SourceEntry]:
    if not data_dir.is_dir():
        raise PakError(f"pasta data não encontrada: {data_dir}")

    root = data_dir.resolve()
    output_resolved = output.resolve(strict=False)
    try:
        output_resolved.relative_to(root)
    except ValueError:
        pass
    else:
        raise PakError("o PAK de saída não pode ficar dentro da pasta data")

    entries: list[SourceEntry] = []
    for source in data_dir.rglob("*"):
        if source.is_symlink():
            raise PakError(f"links simbólicos não são suportados: {source}")
        if source.is_dir():
            continue
        if not source.is_file():
            raise PakError(f"tipo de arquivo não suportado: {source}")

        relative = source.relative_to(data_dir)
        archive_path = Path("data", relative).as_posix().replace("/", "\\")
        try:
            archived_name = archive_path.encode("utf-8") + b"\0"
        except UnicodeEncodeError as exc:
            raise PakError(f"nome de arquivo inválido: {source}") from exc

        size = source.stat().st_size
        if len(archived_name) > MAX_ARCHIVED_NAME_SIZE:
            raise PakError(
                f"caminho interno excede {MAX_ARCHIVED_NAME_SIZE - 1} bytes: {source}"
            )
        if size > UINT32_MAX:
            raise PakError(f"arquivo maior que 4 GiB: {source}")
        entries.append(SourceEntry(source, archived_name, size))

    entries.sort(key=lambda entry: entry.archived_name.lower())
    for previous, current in zip(entries, entries[1:]):
        if previous.archived_name.lower() == current.archived_name.lower():
            raise PakError(
                "caminhos duplicados sem distinção de maiúsculas: "
                f"{previous.source} e {current.source}"
            )
    return entries


def write_archive(output_stream, entries: list[SourceEntry]) -> None:
    output_stream.write(MAGIC)
    output_stream.write(UINT32.pack(VERSION))

    indexed_entries: list[tuple[SourceEntry, int]] = []
    for entry in entries:
        offset = output_stream.tell()
        if offset > UINT32_MAX or offset + entry.size > UINT32_MAX:
            raise PakError("o PAK excederia o limite de 4 GiB do formato")

        with entry.source.open("rb") as source_stream:
            copied = 0
            while True:
                chunk = source_stream.read(COPY_BUFFER_SIZE)
                if not chunk:
                    break
                output_stream.write(chunk)
                copied += len(chunk)
        if copied != entry.size:
            raise PakError(f"arquivo mudou durante a leitura: {entry.source}")
        indexed_entries.append((entry, offset))

    index_offset = output_stream.tell()
    if index_offset > UINT32_MAX:
        raise PakError("posição do índice excede o limite de 4 GiB")

    for entry, offset in indexed_entries:
        entry_size = INDEX_ENTRY.size + len(entry.archived_name)
        output_stream.write(INDEX_ENTRY.pack(entry_size, offset, entry.size))
        output_stream.write(entry.archived_name)
    output_stream.write(UINT32.pack(index_offset))


def build_pak(data_dir: Path, output: Path, force: bool) -> int:
    if output.exists() and not force:
        raise PakError(f"arquivo já existe: {output} (use --force para substituir)")

    entries = collect_files(data_dir, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output_mode = stat.S_IMODE(output.stat().st_mode)
    else:
        current_umask = os.umask(0)
        os.umask(current_umask)
        output_mode = 0o666 & ~current_umask

    temporary_name: Optional[str] = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b", prefix=f".{output.name}.", dir=output.parent, delete=False
        ) as temporary:
            temporary_name = temporary.name
            write_archive(temporary, entries)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, output_mode)
        os.replace(temporary_name, output)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)

    return len(entries)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Gera BOR.PAK do OpenBOR a partir da pasta data."
    )
    parser.add_argument(
        "-f", "--force", action="store_true", help="substitui BOR.PAK se já existir"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    data_dir = Path("data")
    output_pak = Path("BOR.PAK")
    try:
        count = build_pak(data_dir, output_pak, args.force)
        print(f"{output_pak}: {count} arquivo(s) empacotados.")
        return 0
    except (OSError, PakError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
