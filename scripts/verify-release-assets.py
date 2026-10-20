#!/usr/bin/env python3
"""Verify producer checksums and exact Browser release layout before publication.

This proves artifact integrity, not runtime, publisher-signature or UI acceptance.
Only the Python standard library is required.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import sys

class IntegrityError(ValueError):
    """A release cannot be accepted for publication."""

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def valid_release_version(version: str) -> bool:
    """Bounded linear-time ASCII parser for the existing release version grammar."""
    if len(version) > 80:
        return False
    core, dash, suffix = version.partition("-")
    parts = core.split(".")
    if len(parts) != 3 or any(
        not part or not part.isascii() or not part.isdecimal() for part in parts
    ):
        return False
    if not dash:
        return True
    return (
        bool(suffix)
        and suffix.isascii()
        and suffix[0].isalnum()
        and all(char.isalnum() or char in ".-" for char in suffix[1:])
    )


def verify(version: str, directory: Path) -> dict[str, str]:
    if not valid_release_version(version):
        raise IntegrityError("Invalid release version")
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise IntegrityError("Release directory must be a real directory")
    setup = f"Remote-Commander-Browser-Setup-v{version}.exe"
    windows = f"remote-commander-browser-v{version}-windows-x86_64.zip"
    linux = f"remote-commander-browser-v{version}-linux-x86_64.tar.gz"
    pairs = {
        f"SHA256SUMS-setup-v{version}.txt": setup,
        f"SHA256SUMS-windows-v{version}.txt": windows,
        f"{linux}.sha256": linux,
    }
    expected = set(pairs) | set(pairs.values())
    actual = {p.name for p in directory.iterdir()}
    if actual != expected:
        raise IntegrityError(
            f"Unexpected release layout: missing={sorted(expected-actual)} extra={sorted(actual-expected)}"
        )
    for name in expected:
        member = directory / name
        if member.is_symlink() or not member.is_file():
            raise IntegrityError(f"Release member is not a regular non-symlink file: {name}")
    verified = {}
    for manifest, asset in pairs.items():
        manifest_path = directory / manifest
        if manifest_path.stat().st_size > 512:
            raise IntegrityError(f"Oversized checksum manifest: {manifest}")
        try:
            lines = manifest_path.read_text(encoding="ascii").splitlines()
        except UnicodeError as exc:
            raise IntegrityError(f"Non-ASCII checksum manifest: {manifest}") from exc
        if len(lines) != 1:
            raise IntegrityError(f"Expected one checksum record: {manifest}")
        # The checksum record is ASCII and at most 512 bytes (guarded above).
        # Exact-width slicing avoids a potentially costly regex on user-supplied
        # manifest content while preserving the strict portable-name policy.
        record = lines[0]
        digest = record[:64]
        checksum_name = record[66:]
        if (
            len(record) != 66 + len(asset)
            or record[64:66] != "  "
            or checksum_name != asset
            or any(ch not in "0123456789abcdefABCDEF" for ch in digest)
        ):
            raise IntegrityError(f"Checksum must name exactly the portable asset basename: {manifest}")
        actual_hash = sha256(directory / asset)
        if actual_hash != digest.lower():
            raise IntegrityError(f"Producer checksum mismatch: {asset}")
        verified[asset] = actual_hash
    return verified

def trusted_cli_release_stage(value: Path) -> Path:
    # A publishing command accepts only its own fixed dist/release directory.
    # Pure verify() may still inspect disposable test fixtures.
    repo_root = Path(__file__).resolve().parent.parent
    if Path.cwd().resolve() != repo_root or value != Path("dist") / "release":
        raise IntegrityError("RELEASE_STAGING_DIRECTORY_DENIED")
    dist_root = repo_root / "dist"
    candidate = dist_root / "release"
    # Checking only the leaf misses an attacker-controlled dist -> outside symlink.
    # All ancestors below the resolved repository root must be real directories.
    if (
        not dist_root.is_dir()
        or dist_root.is_symlink()
        or (hasattr(dist_root, "is_junction") and dist_root.is_junction())
        or not candidate.is_dir()
        or candidate.is_symlink()
        or (hasattr(candidate, "is_junction") and candidate.is_junction())
        or candidate.resolve() != repo_root / "dist" / "release"
    ):
        raise IntegrityError("RELEASE_STAGING_DIRECTORY_DENIED")
    return candidate


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        verified = verify(args.version, trusted_cli_release_stage(args.directory))
    except (IntegrityError, OSError) as exc:
        print(f"RELEASE_PRODUCER_SHA256_FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"RELEASE_PRODUCER_SHA256_PASS version={args.version} payloads={len(verified)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
