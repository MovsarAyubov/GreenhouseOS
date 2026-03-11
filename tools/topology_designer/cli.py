#!/usr/bin/env python3
"""CLI for human-friendly greenhouse profile compiler."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

if __package__ is None or __package__ == "":
    repo_root = Path(__file__).resolve().parents[2]
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))

from tools.topology import topology_packer
from tools.topology_designer.profile_compiler import (
    ProfileError,
    compile_profile,
    load_profile,
    profile_summary,
    write_json,
)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate", help="Validate greenhouse_profile_v1 JSON")
    validate_parser.add_argument("--input", required=True, type=Path, help="Path to greenhouse profile JSON")

    compile_parser = subparsers.add_parser("compile", help="Compile profile into topology_config_v2 JSON")
    compile_parser.add_argument("--input", required=True, type=Path, help="Path to greenhouse profile JSON")
    compile_parser.add_argument("--output", required=True, type=Path, help="Output path for topology JSON")
    compile_parser.add_argument("--indent", type=int, default=2, help="JSON indentation")

    pack_parser = subparsers.add_parser("pack", help="Compile profile and run topology packer")
    pack_parser.add_argument("--input", required=True, type=Path, help="Path to greenhouse profile JSON")
    pack_parser.add_argument("--output-bin", required=True, type=Path, help="Output .bin path")
    pack_parser.add_argument("--output-chunks", required=True, type=Path, help="Output chunks JSON path")
    pack_parser.add_argument("--output-topology", type=Path, default=None, help="Optional compiled topology JSON path")
    pack_parser.add_argument("--start-token", type=int, default=1, help="First submit token (1..65535)")
    pack_parser.add_argument(
        "--chunk-words",
        type=int,
        default=topology_packer.TOPOLOGY_UPLOAD_CHUNK_WORDS,
        help=f"Chunk words (1..{topology_packer.TOPOLOGY_UPLOAD_CHUNK_WORDS})",
    )
    pack_parser.add_argument("--indent", type=int, default=2, help="JSON indentation")

    return parser


def _run_validate(args: argparse.Namespace) -> int:
    profile = load_profile(args.input)
    summary = profile_summary(profile)
    print(
        f"Profile valid. zones={summary['zones']} points={summary['points']} "
        f"poll_util={summary['poll_util_permille']} permille"
    )
    return 0


def _run_compile(args: argparse.Namespace) -> int:
    profile = load_profile(args.input)
    topology = compile_profile(profile)
    write_json(args.output, topology, indent=max(args.indent, 0))
    summary = profile_summary(profile)
    print(
        f"Topology compiled: zones={summary['zones']} points={summary['points']} "
        f"poll_util={summary['poll_util_permille']} permille"
    )
    print(f"Output topology JSON: {args.output}")
    return 0


def _run_pack(args: argparse.Namespace) -> int:
    profile = load_profile(args.input)
    topology = compile_profile(profile)
    if args.output_topology is not None:
        write_json(args.output_topology, topology, indent=max(args.indent, 0))

    blob = topology_packer.build_topology_blob(topology)
    generation = int(topology["generation"])
    if generation < 0 or generation > 0xFFFFFFFF:
        raise ProfileError(f"generation: value {generation} out of range [0..4294967295]")
    chunks = topology_packer.build_upload_chunks(
        blob=blob,
        generation=generation,
        start_token=args.start_token,
        chunk_words=args.chunk_words,
    )

    args.output_bin.parent.mkdir(parents=True, exist_ok=True)
    args.output_bin.write_bytes(blob)
    output_chunks_payload = {
        "format": "topology_upload_v2",
        "blob_size": len(blob),
        "chunk_words_max": args.chunk_words,
        "chunks_total": len(chunks),
        "chunks": chunks,
    }
    args.output_chunks.parent.mkdir(parents=True, exist_ok=True)
    args.output_chunks.write_text(
        json.dumps(output_chunks_payload, indent=max(args.indent, 0)),
        encoding="utf-8",
    )
    summary = profile_summary(profile)
    print(
        f"Profile packed: zones={summary['zones']} points={summary['points']} "
        f"poll_util={summary['poll_util_permille']} permille"
    )
    print(f"Packed topology blob: {len(blob)} bytes")
    print(f"Prepared chunk requests: {len(chunks)}")
    print(f"Binary output: {args.output_bin}")
    print(f"Chunk script output: {args.output_chunks}")
    if args.output_topology is not None:
        print(f"Compiled topology JSON: {args.output_topology}")
    return 0


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()
    try:
        if args.command == "validate":
            return _run_validate(args)
        if args.command == "compile":
            return _run_compile(args)
        if args.command == "pack":
            return _run_pack(args)
        raise ProfileError(f"unsupported command: {args.command}")
    except (ProfileError, topology_packer.TopologyPackError) as exc:
        print(f"topology_designer error: {exc}")
        return 1
    except OSError as exc:
        print(f"topology_designer error: io failure: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
