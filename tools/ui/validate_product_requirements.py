#!/usr/bin/env python3
"""Validate a scoped, unaccepted UI checkpoint against an immutable Git baseline.

This audit preserves normative requirement fields and migration records. It does
not prove implementation completeness or qualify tests, screenshots or artwork.
Hash deferral is explicitly reported as incomplete and is only for preparation.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


class AuditError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AuditError(message)


def read_json(text: str):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(AuditError(f"Nonfinite JSON value: {value}")))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def repaired_dash(text: str) -> str:
    # Only the observed reversible en-dash encoding defect is admissible in an
    # otherwise immutable normative field. No fuzzy or replacement decoding.
    correct = "\u2013"
    once = correct.encode("utf-8").decode("cp1252")
    twice = once.encode("utf-8").decode("cp1252")
    require(twice.encode("cp1252").decode("utf-8") == once and
            once.encode("cp1252").decode("utf-8") == correct, "Encoding repair is not reversible")
    return text.replace(twice, correct).replace(once, correct)


def validate(args) -> dict:
    root = args.root.resolve()
    def git(*items):
        return subprocess.check_output(["git", "-c", "core.safecrlf=false", *items], cwd=root).decode("utf-8")
    revision = git("rev-parse", "--verify", args.baseline + "^{commit}").strip()
    register_path = (args.register or root / "docs/dev/ui/product-requirements.json").resolve()
    markdown_path = root / "docs/dev/ui/product-requirements.md"
    current = read_json(register_path.read_text(encoding="utf-8"))
    previous = read_json(git("show", revision + ":docs/dev/ui/product-requirements.json"))
    require(current.keys() == previous.keys(), "Register top-level schema changed")
    for key in ("schema_version", "id_namespace", "milestones", "schema_contract", "evidence_policy", "groups", "audit_findings"):
        require(current[key] == previous[key], f"Protected register contract changed: {key}")
    for key, value in previous["baseline"].items():
        if key != "engine_revision":
            require(current["baseline"][key] == value, f"Protected baseline field changed: {key}")
    require(current["baseline"]["engine_revision"] == revision, "Current increment must identify the audited baseline revision")
    require(current["normative_sources"].keys() == previous["normative_sources"].keys(), "Normative source set changed")
    for name, source in previous["normative_sources"].items():
        require({k: v for k, v in source.items() if k != "sha256"} ==
                {k: v for k, v in current["normative_sources"][name].items() if k != "sha256"},
                f"Normative source authority/path changed: {name}")

    before = {row["id"]: row for row in previous["requirements"]}
    rows = {row["id"]: row for row in current["requirements"]}
    require(len(rows) == len(current["requirements"]) == len(before), "Requirement IDs/count changed or duplicated")
    require([row["id"] for row in current["requirements"]] ==
            [row["id"] for row in previous["requirements"]], "Requirement order/identity changed")
    allowed = set(args.partial)
    require(allowed <= rows.keys(), "Unknown --partial requirement")
    changes, status_changes, repairs = {}, {}, {}
    for name, row in rows.items():
        old = before[name]
        require(row.keys() == old.keys(), f"Requirement schema changed: {name}")
        require(re.fullmatch(r"[A-Z]+-[0-9]{3}", name) is not None and name.split("-")[0] in current["groups"], f"Invalid grouped identity: {name}")
        require(row["milestone_owner"] in current["milestones"], f"Unknown milestone: {name}")
        require(bool(row["remaining"].strip()), f"Missing remaining scope: {name}")
        require(row["status"] in current["schema_contract"]["status_values"], f"Unknown status: {name}")
        require(all(dep in rows for dep in row["dependencies"]), f"Unknown dependency: {name}")
        require(len(row["dependencies"]) == len(set(row["dependencies"])), f"Duplicate dependency: {name}")
        require(all(ref.split(":", 1)[0] in current["normative_sources"] for ref in row["source_refs"]), f"Unknown normative source: {name}")
        for field in ("current_evidence", "acceptance_evidence"):
            require(len(row[field]) == len(set(row[field])) and all(ref in current["evidence"] for ref in row[field]), f"Invalid evidence reference: {name}.{field}")
        require(set(old["current_evidence"]) <= set(row["current_evidence"]), f"Historical requirement evidence removed: {name}")
        if row["status"] == "verified":
            require(bool(row["acceptance_evidence"]), f"Verified requirement lacks acceptance evidence: {name}")
        else:
            require(not row["acceptance_evidence"], f"Unaccepted requirement has acceptance evidence: {name}")
        changed = []
        for field, value in row.items():
            if value == old[field]:
                continue
            changed.append(field)
            if field == "requirement" and isinstance(value, str) and repaired_dash(old[field]) == value:
                repairs[name] = {"before": old[field], "after": value}
            elif field == "status":
                require(name in allowed and old[field] == "pending" and value == "partial", f"Unauthorized acceptance/status change: {name}")
                status_changes[name] = {"before": old[field], "after": value}
            else:
                require(field in ("remaining", "current_evidence"), f"Normative scope/constraints/acceptance changed: {name}.{field}")
        if changed:
            changes[name] = changed
        if name.startswith(("GATE-", "FINAL-")):
            require(row["status"] == "pending", f"Checkpoint cannot accept a product gate: {name}")
    require(allowed == status_changes.keys(), "--partial list must match actual pending-to-partial changes exactly")

    visited = set()
    def visit(name, active):
        require(name not in active, f"Dependency cycle: {name}")
        if name not in visited:
            for dep in rows[name]["dependencies"]:
                visit(dep, active | {name})
            visited.add(name)
    for name in rows:
        visit(name, set())
    require(all(name in rows for group in current["audit_findings"].values() for name in group), "Unresolved audit finding")
    require(set(current["audit_findings"]) == {f"UI-{i:02}" for i in range(1, 13)}, "Incomplete audit finding map")
    require(len([name for name in rows if name.startswith("FINAL-")]) == 7, "Incomplete final audit map")
    for name, item in previous["evidence"].items():
        require(name in current["evidence"], f"Historical evidence removed: {name}")
        require({k: v for k, v in item.items() if k != "sha256"} ==
                {k: v for k, v in current["evidence"][name].items() if k != "sha256"}, f"Historical evidence scope/result/binding changed: {name}")

    manifest_path = root / "docs/dev/ui/migration-manifest.json"
    manifest = manifest_path.read_text(encoding="utf-8")
    require(manifest.replace("\r\n", "\n") == git("show", revision + ":docs/dev/ui/migration-manifest.json").replace("\r\n", "\n"), "Migration source records changed")
    resources = read_json(manifest)["resources"]
    require(len(resources) == current["baseline"]["effective_gui_count"], "Migration inventory denominator changed")
    require(all(row["translation_status"] == "pending" and not row["replacement"] and not row["behavior_evidence"] and not row["visual_evidence"] for row in resources), "Checkpoint cannot accept a GUI migration")

    files, deferred, historical_unbound = {}, [], []
    for group in ("normative_sources", "evidence"):
        for name, item in current[group].items():
            for path_key, hash_key in (("path", "sha256"), ("local_capture_summary", "local_capture_summary_sha256")):
                if path_key not in item:
                    continue
                source = (root / item[path_key]).resolve()
                require(source.is_relative_to(root) and source.is_file(), f"Missing/outside evidence path: {name}.{path_key}")
                actual = digest(source)
                files[str(source.relative_to(root)).replace("\\", "/")] = actual
                old_item = previous[group].get(name, {})
                if (group == "evidence" and path_key == "local_capture_summary" and
                        old_item.get(path_key) == item[path_key] and hash_key not in old_item and hash_key not in item):
                    # Preserve the historical record exactly. Observing its
                    # present bytes cannot retroactively bind the old capture.
                    historical_unbound.append({"evidence": name, "path": item[path_key],
                        "observed_sha256": actual, "baseline_hash_binding": False})
                    continue
                if item.get(hash_key) != actual:
                    require(args.defer_hashes, f"Missing/stale source binding: {name}.{hash_key}")
                    deferred.append(f"{group}.{name}.{hash_key}")
    links = 0
    for path in (markdown_path, root / "docs/dev/plans/ui-product-completion.md", root / "docs/dev/ui/value-controls.md"):
        content = path.read_text(encoding="utf-8")
        require(repaired_dash(content) == content and "\ufffd" not in content, f"Mojibake/replacement characters remain: {path}")
        for link in re.findall(r"\]\(([^)]+)\)", content):
            if "://" in link or link.startswith("#"):
                continue
            require((path.parent / link.split("#")[0]).resolve().exists(), f"Broken local link: {path}: {link}")
            links += 1
    counts = Counter(row["status"] for row in rows.values())
    count_text = f"{counts['partial']} partial, {counts['pending']} pending and one verified requirement"
    require(count_text in markdown_path.read_text(encoding="utf-8"), "Markdown status totals are stale")
    return {
        "schema_version": 1, "status": "structure_valid_hashes_deferred" if args.defer_hashes else "structural_source_audit_passed",
        "product_acceptance": False, "baseline_engine_revision": revision,
        "companion_revision": current["baseline"]["companion_revision"],
        "register_sha256": digest(register_path), "markdown_sha256": digest(markdown_path),
        "plan_sha256": digest(root / "docs/dev/plans/ui-product-completion.md"),
        "validator_sha256": digest(Path(__file__).resolve()),
        "requirements": len(rows), "status_counts": dict(counts), "status_changes": status_changes,
        "requirement_field_changes": changes, "strict_roundtrip_text_repairs": repairs,
        "scope_constraints_owners_dependencies_and_acceptance_preserved": True,
        "dependency_cycles": 0, "all_milestone_and_final_gates_pending": True,
        "historical_evidence_preserved_except_document_hashes": True,
        "migration_manifest_unchanged": True, "migration_manifest_sha256": digest(manifest_path),
        "migration_entries": len(resources), "accepted_migration_entries": 0,
        "required_source_bindings_valid": not args.defer_hashes,
        "source_paths_and_hashes_valid": not args.defer_hashes and not historical_unbound,
        "historical_unbound_references": historical_unbound, "deferred_hash_bindings": deferred,
        "source_hashes": files, "local_links_checked": links,
        "limitations": "Structural/source-binding audit only; no build, test, runtime, artwork or product acceptance is inferred from linked evidence. Historical unbound summary pointers are preserved and explicitly reported; their observed current bytes do not establish a historical capture hash."
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--baseline", required=True, help="Immutable existing Git commit, normally the increment's starting HEAD")
    parser.add_argument("--partial", action="append", default=[], metavar="ID", help="Explicitly permitted pending-to-partial transition; never accepts a requirement")
    parser.add_argument("--register", type=Path, help="Optional candidate register for review/negative-case validation")
    parser.add_argument("--defer-hashes", action="store_true", help="Preparation only: report unfinalized source bindings, never report full validation")
    parser.add_argument("--output", type=Path, required=True, help="New report path; existing evidence is never overwritten")
    args = parser.parse_args()
    try:
        require(not args.output.exists(), "Preserve existing audit; choose a new output path")
        report = validate(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        print(f"{report['status']}: {args.output}; SHA256 {digest(args.output)}")
        return 0
    except (AuditError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"Requirement audit failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
