#!/usr/bin/env python3
"""Validate a scoped, unaccepted UI checkpoint against an immutable Git baseline.

This audit preserves normative requirement fields and migration records. It does
not prove implementation completeness or qualify tests, screenshots or artwork.
Hash deferral is explicitly reported as incomplete and is only for preparation.

An implementation increment keeps every requirement and changes only evidence,
remaining scope and explicit pending-to-partial states. A register revision
(--revision) instead appends requirements and records supersessions for a
changed normative source; it cannot edit, remove or accept existing rows.
Both may re-bind document hashes. A binding matches its file in either
line-ending form, so LF and CRLF checkouts of one commit audit alike.
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


REQUIREMENT_FIELDS = ("id", "title", "milestone_owner", "implementation_owner", "source_refs", "requirement",
                      "dependencies", "acceptance_evidence_needed", "status", "current_evidence",
                      "acceptance_evidence", "remaining")
SUPERSESSION_FIELDS = ("superseded", "successors", "normative_change", "source_refs", "carried_evidence")


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


def line_ending_digests(path: Path) -> set:
    # Checkouts differ only by text=auto line endings (LF or CRLF), so a
    # binding made on either kind of checkout matches the same content.
    data = path.read_bytes()
    lf = data.replace(b"\r\n", b"\n")
    return {hashlib.sha256(variant).hexdigest() for variant in (data, lf, lf.replace(b"\n", b"\r\n"))}


def repaired_dash(text: str) -> str:
    # Only the observed reversible en-dash encoding defect is admissible in an
    # otherwise immutable normative field. No fuzzy or replacement decoding.
    correct = "–"
    once = correct.encode("utf-8").decode("cp1252")
    twice = once.encode("utf-8").decode("cp1252")
    require(twice.encode("cp1252").decode("utf-8") == once and
            once.encode("cp1252").decode("utf-8") == correct, "Encoding repair is not reversible")
    return text.replace(twice, correct).replace(once, correct)


def require_acyclic(rows: dict) -> None:
    visited = set()
    def visit(name, active):
        require(name not in active, f"Dependency cycle: {name}")
        if name not in visited:
            for dep in rows[name]["dependencies"]:
                visit(dep, active | {name})
            visited.add(name)
    for name in rows:
        visit(name, set())


def check_migration_manifest(root: Path, git, revision: str, current: dict):
    manifest_path = root / "docs/dev/ui/migration-manifest.json"
    manifest = manifest_path.read_text(encoding="utf-8")
    require(manifest.replace("\r\n", "\n") == git("show", revision + ":docs/dev/ui/migration-manifest.json").replace("\r\n", "\n"), "Migration source records changed")
    resources = read_json(manifest)["resources"]
    require(len(resources) == current["baseline"]["effective_gui_count"], "Migration inventory denominator changed")
    require(all(row["translation_status"] == "pending" and not row["replacement"] and not row["behavior_evidence"] and not row["visual_evidence"] for row in resources), "Checkpoint cannot accept a GUI migration")
    return manifest_path, resources


def check_source_bindings(root: Path, current: dict, previous: dict, defer_hashes: bool):
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
                if item.get(hash_key) not in line_ending_digests(source):
                    require(defer_hashes, f"Missing/stale source binding: {name}.{hash_key}")
                    deferred.append(f"{group}.{name}.{hash_key}")
    return files, deferred, historical_unbound


def check_markdown(root: Path, markdown_path: Path, rows: dict):
    links = 0
    for path in (markdown_path, root / "docs/dev/plans/ui-product-completion.md", root / "docs/dev/ui/value-controls.md"):
        content = path.read_text(encoding="utf-8")
        require(repaired_dash(content) == content and "�" not in content, f"Mojibake/replacement characters remain: {path}")
        for link in re.findall(r"\]\(([^)]+)\)", content):
            if "://" in link or link.startswith("#"):
                continue
            require((path.parent / link.split("#")[0]).resolve().exists(), f"Broken local link: {path}: {link}")
            links += 1
    counts = Counter(row["status"] for row in rows.values())
    count_text = f"{counts['partial']} partial, {counts['pending']} pending and one verified requirement"
    require(count_text in markdown_path.read_text(encoding="utf-8"), "Markdown status totals are stale")
    return links, counts


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
    protected = ("schema_version", "id_namespace", "milestones", "schema_contract", "evidence_policy", "groups", "audit_findings")
    # Supersessions are recorded only by a register revision (--revision).
    protected += ("supersessions",) if "supersessions" in previous else ()
    for key in protected:
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

    require_acyclic(rows)
    require(all(name in rows for group in current["audit_findings"].values() for name in group), "Unresolved audit finding")
    require(set(current["audit_findings"]) == {f"UI-{i:02}" for i in range(1, 13)}, "Incomplete audit finding map")
    require(len([name for name in rows if name.startswith("FINAL-")]) == 7, "Incomplete final audit map")
    for name, item in previous["evidence"].items():
        require(name in current["evidence"], f"Historical evidence removed: {name}")
        require({k: v for k, v in item.items() if k != "sha256"} ==
                {k: v for k, v in current["evidence"][name].items() if k != "sha256"}, f"Historical evidence scope/result/binding changed: {name}")

    manifest_path, resources = check_migration_manifest(root, git, revision, current)
    files, deferred, historical_unbound = check_source_bindings(root, current, previous, args.defer_hashes)
    links, counts = check_markdown(root, markdown_path, rows)
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


def clean_text(value) -> bool:
    return isinstance(value, str) and bool(value.strip()) and repaired_dash(value) == value and "�" not in value


def validate_revision(args) -> dict:
    root = args.root.resolve()
    def git(*items):
        return subprocess.check_output(["git", "-c", "core.safecrlf=false", *items], cwd=root).decode("utf-8")
    revision = git("rev-parse", "--verify", args.baseline + "^{commit}").strip()
    register_path = (args.register or root / "docs/dev/ui/product-requirements.json").resolve()
    markdown_path = root / "docs/dev/ui/product-requirements.md"
    current = read_json(register_path.read_text(encoding="utf-8"))
    previous = read_json(git("show", revision + ":docs/dev/ui/product-requirements.json"))
    require(not args.partial, "A register revision cannot change requirement status; audit status changes as an increment")

    # Contract: keep every existing entry; only additions are admissible.
    require(set(previous) <= set(current) and set(current) - set(previous) <= {"supersessions"},
            "Register top-level schema changed beyond a supersession record")
    require(isinstance(current["schema_version"], int) and current["schema_version"] >= previous["schema_version"], "Register schema version decreased")
    for key in ("id_namespace", "milestones"):
        require(current[key] == previous[key], f"Protected register contract changed: {key}")
    for key in ("groups", "schema_contract"):
        require(all(current[key].get(name) == value for name, value in previous[key].items()),
                f"A register revision cannot edit an existing {key} entry")
    old_policy, new_policy = previous["evidence_policy"], current["evidence_policy"]
    require(new_policy.keys() == old_policy.keys() and
            new_policy["per_acceptance_record_required"] == old_policy["per_acceptance_record_required"] and
            new_policy["constraints"][:len(old_policy["constraints"])] == old_policy["constraints"],
            "A register revision may only append evidence policy constraints")
    require(all(clean_text(text) for text in new_policy["constraints"]), "Invalid evidence policy constraint text")
    for key, value in previous["baseline"].items():
        if key != "engine_revision":
            require(current["baseline"][key] == value, f"Protected baseline field changed: {key}")
    require(current["baseline"]["engine_revision"] == revision, "Current revision must identify the audited baseline revision")
    require(current["normative_sources"].keys() == previous["normative_sources"].keys(), "Normative source set changed")
    for name, source in previous["normative_sources"].items():
        require({k: v for k, v in source.items() if k != "sha256"} ==
                {k: v for k, v in current["normative_sources"][name].items() if k != "sha256"},
                f"Normative source authority/path changed: {name}")
    # As in an increment, evidence records keep their scope and result; only a
    # document hash may be re-bound, and the binding check below verifies it.
    require(current["evidence"].keys() == previous["evidence"].keys(), "A register revision cannot add or remove evidence records")
    for name, item in previous["evidence"].items():
        require({k: v for k, v in item.items() if k != "sha256"} ==
                {k: v for k, v in current["evidence"][name].items() if k != "sha256"}, f"Historical evidence scope/result/binding changed: {name}")
    rebound = {f"{group}.{name}": {"before": item.get("sha256"), "after": current[group][name].get("sha256")}
               for group in ("normative_sources", "evidence") for name, item in previous[group].items()
               if item.get("sha256") != current[group][name].get("sha256")}

    # Existing requirements stay byte-identical and in order; new ones append.
    old_rows = previous["requirements"]
    require([row["id"] for row in current["requirements"][:len(old_rows)]] == [row["id"] for row in old_rows],
            "Existing requirement order/identity changed")
    for old, row in zip(old_rows, current["requirements"]):
        require(row == old, f"A register revision cannot edit an existing requirement: {old['id']}")
    rows = {row["id"]: row for row in current["requirements"]}
    require(len(rows) == len(current["requirements"]), "Duplicate requirement ID")
    added = current["requirements"][len(old_rows):]
    added_ids = [row["id"] for row in added]
    last_number = {}
    for row in old_rows:
        group, number = row["id"].split("-")
        last_number[group] = max(last_number.get(group, 0), int(number))
    for row in added:
        name = row["id"]
        require(isinstance(name, str) and re.fullmatch(r"[A-Z]+-[0-9]{3}", name) is not None and name.split("-")[0] in current["groups"],
                f"Invalid grouped identity: {name}")
        group, number = name.split("-")
        require(int(number) == last_number.get(group, 0) + 1, f"Appended IDs must continue their group sequence: {name}")
        last_number[group] = int(number)
        require(tuple(row)[:len(REQUIREMENT_FIELDS)] == REQUIREMENT_FIELDS and set(row) <= set(REQUIREMENT_FIELDS) | {"constraints"},
                f"Requirement schema differs: {name}")
        owner = current["groups"][group]
        require(row["implementation_owner"] == owner["implementation_owner"], f"Implementation owner differs from its group: {name}")
        require(all(ref in row["source_refs"] for ref in owner["source_refs"]) and
                all(ref.split(":", 1)[0] in current["normative_sources"] for ref in row["source_refs"]),
                f"Source references must include the group references and known sources: {name}")
        require(row["milestone_owner"] in current["milestones"], f"Unknown milestone: {name}")
        require(all(clean_text(row[field]) for field in ("title", "requirement", "remaining")), f"Invalid requirement text: {name}")
        require(bool(row["acceptance_evidence_needed"]) and all(clean_text(text) for text in row["acceptance_evidence_needed"]),
                f"Missing acceptance evidence description: {name}")
        require(row["status"] in ("pending", "partial"), f"A register revision cannot accept a requirement: {name}")
        require(not row["acceptance_evidence"], f"Unaccepted requirement has acceptance evidence: {name}")
        require(row["status"] == "pending" or bool(row["current_evidence"]), f"Partial requirement needs current evidence: {name}")
        require(len(row["current_evidence"]) == len(set(row["current_evidence"])) and
                all(ref in current["evidence"] for ref in row["current_evidence"]), f"Invalid evidence reference: {name}.current_evidence")
        require(all(dep in rows and dep != name for dep in row["dependencies"]) and
                len(row["dependencies"]) == len(set(row["dependencies"])), f"Invalid dependency: {name}")
        require("constraints" not in row or (isinstance(row["constraints"], dict) and row["constraints"]), f"Empty constraints: {name}")
    require_acyclic(rows)
    require(len([name for name in rows if name.startswith("FINAL-")]) == 7, "Incomplete final audit map")

    # Supersessions link replaced requirements to successors appended here.
    old_records = previous.get("supersessions", [])
    records = current.get("supersessions", [])
    require(isinstance(records, list) and records[:len(old_records)] == old_records, "Recorded supersessions changed")
    superseded = [record["superseded"] for record in old_records]
    new_records = records[len(old_records):]
    for record in new_records:
        require(tuple(record) == SUPERSESSION_FIELDS, f"Supersession schema differs: {record}")
        name = record["superseded"]
        require(name in {row["id"] for row in old_rows}, f"Only an existing requirement can be superseded: {name}")
        require(name not in superseded, f"Requirement superseded twice: {name}")
        superseded.append(name)
        successors = record["successors"]
        require(bool(successors) and len(successors) == len(set(successors)) and all(item in added_ids for item in successors),
                f"Successors must be requirements appended by this revision: {name}")
        require(clean_text(record["normative_change"]), f"Missing normative change: {name}")
        require(bool(record["source_refs"]) and all(ref.split(":", 1)[0] in current["normative_sources"] for ref in record["source_refs"]),
                f"Invalid supersession source reference: {name}")
        carried = record["carried_evidence"]
        require(all(item in rows[name]["current_evidence"] for item in carried) and
                all(any(item in rows[successor]["current_evidence"] for successor in successors) for item in carried),
                f"Carried evidence must come from the superseded row and reach a successor: {name}")
    successors = {item for record in records for item in record["successors"]}
    require(not successors & set(superseded), "A superseded requirement cannot be a successor")
    require(bool(added) or bool(new_records), "A register revision must append requirements or record supersessions")

    # Audit findings keep their mapping; successors may be appended.
    require(current["audit_findings"].keys() == previous["audit_findings"].keys(), "Audit finding set changed")
    for finding, names in previous["audit_findings"].items():
        extension = current["audit_findings"][finding][len(names):]
        require(current["audit_findings"][finding][:len(names)] == names and all(item in added_ids for item in extension),
                f"Audit finding may only append requirements added by this revision: {finding}")
    require(all(name in rows for group in current["audit_findings"].values() for name in group), "Unresolved audit finding")

    manifest_path, resources = check_migration_manifest(root, git, revision, current)
    files, deferred, historical_unbound = check_source_bindings(root, current, previous, args.defer_hashes)
    links, counts = check_markdown(root, markdown_path, rows)
    return {
        "schema_version": 1, "status": "revision_hashes_deferred" if args.defer_hashes else "register_revision_audit_passed",
        "product_acceptance": False, "baseline_engine_revision": revision,
        "companion_revision": current["baseline"]["companion_revision"],
        "register_schema_version": current["schema_version"],
        "register_sha256": digest(register_path), "markdown_sha256": digest(markdown_path),
        "validator_sha256": digest(Path(__file__).resolve()),
        "requirements": len(rows), "previous_requirements": len(old_rows), "appended_requirements": added_ids,
        "appended_by_group": dict(Counter(name.split("-")[0] for name in added_ids)),
        "supersessions": {record["superseded"]: record["successors"] for record in new_records},
        "status_counts": dict(counts), "existing_requirements_unchanged": True,
        "evidence_preserved_except_document_hashes": True, "rebound_document_hashes": rebound,
        "dependency_cycles": 0, "all_milestone_and_final_gates_pending": all(
            rows[name]["status"] == "pending" for name in rows if name.startswith(("GATE-", "FINAL-"))),
        "migration_manifest_unchanged": True, "migration_manifest_sha256": digest(manifest_path),
        "migration_entries": len(resources), "accepted_migration_entries": 0,
        "required_source_bindings_valid": not args.defer_hashes,
        "source_paths_and_hashes_valid": not args.defer_hashes and not historical_unbound,
        "historical_unbound_references": historical_unbound, "deferred_hash_bindings": deferred,
        "source_hashes": files, "local_links_checked": links,
        "limitations": "Structural/source-binding audit of a scope revision only; appended requirements are unaccepted and no implementation, artwork or product acceptance is inferred."
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--baseline", required=True, help="Immutable existing Git commit, normally the increment's starting HEAD")
    parser.add_argument("--partial", action="append", default=[], metavar="ID", help="Explicitly permitted pending-to-partial transition; never accepts a requirement")
    parser.add_argument("--register", type=Path, help="Optional candidate register for review/negative-case validation")
    parser.add_argument("--defer-hashes", action="store_true", help="Preparation only: report unfinalized source bindings, never report full validation")
    parser.add_argument("--revision", action="store_true", help="Audit a register revision: appended requirements and recorded supersessions only")
    parser.add_argument("--output", type=Path, required=True, help="New report path; existing evidence is never overwritten")
    args = parser.parse_args()
    try:
        require(not args.output.exists(), "Preserve existing audit; choose a new output path")
        report = validate_revision(args) if args.revision else validate(args)
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
