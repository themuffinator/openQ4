#!/usr/bin/env python3
"""Contract checks for P0 provenance and authoritative capability evidence."""

from __future__ import annotations

import importlib.util
import copy
import hashlib
import sys
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, snippet: str, context: str) -> None:
    if snippet not in text:
        raise AssertionError(f"missing {snippet!r} in {context}")


def test_provenance_inventory() -> None:
    audit = load_module(
        "openq4_source_provenance_audit",
        ROOT / "tools" / "validation" / "audit_source_provenance.py",
    )
    manifest = audit.load_manifest()
    report = audit.inventory(ROOT, manifest)
    failures = audit.validate(ROOT, manifest, report)
    if failures:
        raise AssertionError("provenance validation failed:\n" + "\n".join(failures))

    # Emile's two Android platform adaptations retain the Doom 3 notices.
    assert report["families"]["doom3"]["count"] == 583
    doom3_paths = {entry["localPath"] for entry in report["families"]["doom3"]["files"]}
    assert {"src/sys/android/android_main.cpp", "src/sys/android/android_sdl3.cpp"} <= doom3_paths
    bfg_files = report["families"]["doom3_bfg"]["files"]
    assert len(bfg_files) == 37
    classifications: dict[str, int] = {}
    for entry in bfg_files:
        classification = entry.get("classification", "")
        classifications[classification] = classifications.get(classification, 0) + 1
        assert entry.get("auditedPath")
        assert entry.get("auditedCommit")
        assert entry.get("repository")
    assert classifications == {
        "official-snapshot-path": 31,
        "intermediate-fork-lineage": 6,
    }

    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="provenance-mutation-", dir=ROOT / ".tmp") as temp:
        altered = Path(temp) / "altered-terms.txt"
        original = ROOT / manifest["families"]["doom3"]["localAdditionalTerms"]
        altered.write_bytes(original.read_bytes() + b"altered\n")
        mutated_manifest = copy.deepcopy(manifest)
        mutated_manifest["families"]["doom3"]["localAdditionalTerms"] = altered.relative_to(ROOT).as_posix()
        mutation_failures = audit.validate(ROOT, mutated_manifest, report)
        assert any("SHA-256 differs" in failure for failure in mutation_failures)

        official = Path(temp) / "official"
        official.mkdir()
        copying = official / "COPYING.txt"
        copying.write_bytes(b"published copying bytes\r\n")
        copying_spec = {"officialCopyingSha256": hashlib.sha256(copying.read_bytes()).hexdigest()}
        assert audit.validate_official_copying("test", copying_spec, official) == []
        copying.write_bytes(copying.read_bytes() + b"tampered")
        assert any(
            "official COPYING.txt SHA-256 differs" in failure
            for failure in audit.validate_official_copying("test", copying_spec, official)
        )

    provenance = read("docs/dev/source-provenance.md")
    for snippet in (
        "not a legal opinion",
        "Doom 3 GPL Source Code",
        "Doom 3 BFG Edition GPL Source Code",
        "intermediate lineage reference",
        "audit_source_provenance.py --check",
    ):
        require(provenance, snippet, "source provenance documentation")


def test_accompanying_terms() -> None:
    doom3 = read("LICENSES/DOOM-3-ADDITIONAL-TERMS.txt")
    bfg = read("LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt")
    require(doom3, "ADDITIONAL TERMS APPLICABLE TO THE DOOM 3 GPL SOURCE CODE", "Doom 3 Additional Terms")
    require(bfg, "ADDITIONAL TERMS APPLICABLE TO THE Doom 3 BFG Edition GPL Source Code", "BFG Additional Terms")
    for terms in (doom3, bfg):
        for section in (
            "Replacement of Section 15",
            "Replacement of Section 16",
            "LEGAL NOTICES; NO TRADEMARK LICENSE; ORIGIN",
            "INDEMNIFICATION",
        ):
            require(terms, section, "accompanying Additional Terms")


def test_capability_matrix_is_authoritative_and_scoped() -> None:
    matrix = read("docs/dev/engine-capability-matrix.md")
    require(matrix, "authoritative current-state index", "capability matrix")
    for status in ("**Implemented**", "**Experimental**", "**Missing**"):
        require(matrix, status, "capability matrix status vocabulary")
    for capability in (
        "Network-driven executable updater",
        "Server-supplied package transport",
        "Pure multiplayer game-module boundary",
        "Malformed network and snapshot input handling",
        "Connection challenge entropy",
        "Authenticated remote console (`rcon2`)",
        "Rcon abuse limits and secret redaction",
        "Legacy plaintext rcon",
        "Doom 3 / Doom 3 BFG provenance inventory",
        "Reproducible retail-PK4 SP/MP compatibility evidence",
        "Backend-neutral renderer contracts",
        "Modern visible lighting ownership",
        "Shared classic interaction-lighting ownership",
        "Vulkan renderer",
        "GPU skeletal skinning",
        "Automatic dynamic resolution",
        "Namespaced PBR materials",
    ):
        require(matrix, capability, "capability matrix coverage")
    require(matrix, "`net_clientUseLegacyRcon 1` / `net_serverAllowLegacyRcon 1`", "legacy rcon containment claim")
    require(
        matrix,
        "**Implemented** (scoped Milestone D consumers; broader modern-renderer use pending)",
        "backend-neutral renderer contract scope",
    )
    require(
        matrix,
        "eligible root GUI, world ambient, interaction, fog/blend",
        "four implemented shared domains",
    )
    require(
        matrix,
        "Special-view and nested cinematic/post records share exact root/depth identity",
        "remaining shared-domain boundary",
    )
    require(
        matrix,
        "The domains remain default-off with clean-package/platform promotion gates",
        "shared-domain promotion boundary",
    )

    roadmap = read("docs/dev/idtech5-modernization-roadmap.md")
    require(
        roadmap,
        "domains have retained local runtime qualification",
        "roadmap local domain status",
    )
    require(
        roadmap,
        "root cinematic/post, `_currentDepth`, broad subview forms, in-world GUI, special frame, authored-stock breadth, clean-package, retained-review, and target-platform/driver qualification remain open as applicable",
        "roadmap promotion boundary",
    )

    renderer_matrix = read("docs/dev/renderer-validation-matrix.md")
    proposal = read("docs/dev/proposals/rbdoom3-bfg-parity-modernization-plan.md")
    readme = read("README.md")
    require(renderer_matrix, "engine capability matrix", "renderer evidence federation")
    require(proposal, "historical proposal, not a current-state inventory", "stale proposal warning")
    require(readme, "engine capability matrix", "README capability link")
    require(readme, "source-provenance inventory", "README provenance link")


def test_workflow_registration() -> None:
    validator = read("tools/validation/openq4_validate.py")
    for test_name in ("p0_governance_evidence.py", "stock_asset_baseline.py"):
        require(validator, test_name, "local validation registration")
    for workflow in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        text = read(workflow)
        require(
            text,
            "python tools/validation/audit_source_provenance.py --check",
            workflow,
        )
        for test_name in ("p0_governance_evidence.py", "stock_asset_baseline.py"):
            require(text, f"python tools/tests/{test_name}", workflow)


def main() -> None:
    test_provenance_inventory()
    test_accompanying_terms()
    test_capability_matrix_is_authoritative_and_scoped()
    test_workflow_registration()
    print("p0_governance_evidence: ok")


if __name__ == "__main__":
    main()
