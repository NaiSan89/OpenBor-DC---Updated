#!/usr/bin/env python3
"""Extract files from an OpenBOR PAK archive."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Iterator


MAGIC = b"PACK"
HEADER_SIZE = 8
INDEX_POINTER_SIZE = 4
UINT32 = struct.Struct("<I")
INDEX_ENTRY = struct.Struct("<III")
COPY_BUFFER_SIZE = 1024 * 1024


class PakError(Exception):
    """Raised when a PAK file is invalid or cannot be safely extracted."""


@dataclass(frozen=True)
class PakEntry:
    name: str
    offset: int
    size: int


def read_exact(stream: BinaryIO, size: int, description: str) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise PakError(f"PAK truncado ao ler {description}")
    return data


def decode_name(raw_name: bytes) -> str:
    encoded_name = raw_name.split(b"\0", 1)[0]
    if not encoded_name:
        raise PakError("entrada com nome vazio")
    try:
        return encoded_name.decode("utf-8")
    except UnicodeDecodeError:
        # Old PAK builders used the host's single-byte encoding.
        return encoded_name.decode("latin-1")


def read_index(stream: BinaryIO) -> Iterator[PakEntry]:
    stream.seek(0, 2)
    archive_size = stream.tell()
    if archive_size < HEADER_SIZE + INDEX_POINTER_SIZE:
        raise PakError("arquivo pequeno demais para ser um PAK")

    stream.seek(0)
    if read_exact(stream, 4, "assinatura") != MAGIC:
        raise PakError("assinatura inválida (esperado: PACK)")

    version = UINT32.unpack(read_exact(stream, 4, "versão"))[0]
    if version != 0:
        raise PakError(f"versão PAK não suportada: {version}")

    index_end = archive_size - INDEX_POINTER_SIZE
    stream.seek(index_end)
    index_offset = UINT32.unpack(read_exact(stream, 4, "ponteiro do índice"))[0]
    if not HEADER_SIZE <= index_offset <= index_end:
        raise PakError(f"posição inválida do índice: {index_offset}")

    stream.seek(index_offset)
    while stream.tell() < index_end:
        bytes_left = index_end - stream.tell()
        if bytes_left < INDEX_ENTRY.size:
            raise PakError("índice termina no meio de uma entrada")

        entry_size, file_offset, file_size = INDEX_ENTRY.unpack(
            read_exact(stream, INDEX_ENTRY.size, "entrada do índice")
        )
        name_size = entry_size - INDEX_ENTRY.size
        if name_size <= 0 or name_size > index_end - stream.tell():
            raise PakError(f"tamanho inválido de entrada no índice: {entry_size}")

        raw_name = read_exact(stream, name_size, "nome de arquivo")
        if file_offset < HEADER_SIZE or file_offset + file_size > index_offset:
            raise PakError(
                f"dados fora dos limites para {decode_name(raw_name)!r}: "
                f"offset={file_offset}, tamanho={file_size}"
            )
        yield PakEntry(decode_name(raw_name), file_offset, file_size)

    if stream.tell() != index_end:
        raise PakError("tamanho do índice inconsistente")


def safe_output_path(output_dir: Path, archived_name: str, lowercase: bool) -> Path:
    normalized = archived_name.replace("\\", "/")
    if lowercase:
        normalized = normalized.lower()

    # Muitos PAKs do OpenBOR já armazenam os arquivos dentro de "data/"
    # Evita criar data/data/... durante a extração.
    if normalized.lower().startswith("data/"):
        normalized = normalized[5:]

    relative = PurePosixPath(normalized)
    if (
        relative.is_absolute()
        or not relative.parts
        or any(part in ("", ".", "..") for part in relative.parts)
        or ":" in relative.parts[0]
    ):
        raise PakError(f"caminho inseguro no PAK: {archived_name!r}")

    root = output_dir.resolve()
    destination = root.joinpath(*relative.parts)
    try:
        destination.resolve(strict=False).relative_to(root)
    except ValueError as exc:
        raise PakError(f"caminho escapa da pasta de destino: {archived_name!r}") from exc
    return destination


def copy_entry(
    stream: BinaryIO, entry: PakEntry, destination: Path, overwrite: bool
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and not overwrite:
        raise PakError(
            f"arquivo já existe: {destination} (use --overwrite para substituir)"
        )

    stream.seek(entry.offset)
    remaining = entry.size
    with destination.open("wb") as output:
        while remaining:
            chunk = read_exact(
                stream, min(remaining, COPY_BUFFER_SIZE), f"dados de {entry.name!r}"
            )
            output.write(chunk)
            remaining -= len(chunk)


def find_default_pak() -> Path:
    for name in ("BOR.PAK", "bor.pak"):
        pak = Path(name)
        if pak.exists():
            return pak
    raise PakError("não encontrei BOR.PAK nem bor.pak na pasta atual")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extrai o conteúdo de um arquivo PAK do OpenBOR."
    )
    parser.add_argument(
        "pak",
        nargs="?",
        type=Path,
        help="arquivo PAK de entrada (padrão: BOR.PAK ou bor.pak)",
    )
    parser.add_argument(
        "-f",
        "--overwrite",
        action="store_true",
        help="substitui arquivos que já existam",
    )
    parser.add_argument(
        "--lowercase",
        action="store_true",
        help="converte os caminhos extraídos para letras minúsculas",
    )
    parser.add_argument(
        "-l", "--list", action="store_true", help="apenas lista o conteúdo"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        pak_path = args.pak if args.pak is not None else find_default_pak()
        data_dir = Path("data")
        with pak_path.open("rb") as stream:
            entries = list(read_index(stream))
            if args.list:
                for entry in entries:
                    print(f"{entry.size:10d}  {entry.name}")
            else:
                data_dir.mkdir(parents=True, exist_ok=True)
                for entry in entries:
                    destination = safe_output_path(
                        data_dir, entry.name, args.lowercase
                    )
                    copy_entry(stream, entry, destination, args.overwrite)
                    print(destination)
        action = "listados" if args.list else "extraídos"
        print(f"{len(entries)} arquivo(s) {action}.")
        return 0
    except (OSError, PakError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
