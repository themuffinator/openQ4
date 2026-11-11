#!/usr/bin/env python3
"""Evidence-controlled acceptance transition for the UI requirement register.

This is the only audit that records acceptance (roadmap UI-11). The checkpoint,
revision, archive and inventory audits in validate_product_requirements.py keep
their restrictions: none of them adds a verified requirement, a verified gate or
an accepted migration resource.

A transition names every requirement (--accept) and migration resource
(--accept-resource) it accepts; nothing is accepted by implication, and a record
cannot claim more than the transition accepts. Each acceptance cites committed
records under docs/dev/ui/acceptance/ that carry the evidence policy's
per-record fields, a review by someone other than the record's author, and a
proof for every part of the claim:

- the requirement's exact current text, each of its acceptance-evidence
  conjuncts and each of its machine-readable constraints;
- each dependency, resolved through recorded supersessions: verified with
  current acceptance evidence, or, for a milestone gate, passed at that gate's
  own milestone scope while the broader shared requirement stays partial
  (GATE-003/GATE-004);
- for a final product gate, every applicable requirement verified and every
  migration resource accepted;
- for a migration resource, its replacement document and separate behavior and
  artwork proof, bound to the resource's unchanged source identity.

Every source, asset and normative document a record binds must hold the same
content at the record's engine revision and in the present files, and that
revision must lie in the audited history; any later change makes the record
stale. The checkpoint audit applies the same staleness rule to requirements and
resources accepted earlier, which must then be withdrawn or accepted again.
"""
from __future__ import annotations

from datetime import date
import hashlib
from pathlib import Path
import re
import struct
import subprocess


RECORD_PREFIX = "docs/dev/ui/acceptance/"
RECORD_KIND = "acceptance_record"
RECORD_SCHEMA = 1
RECORD_FIELDS = ("acceptance_schema", "id", "prepared_by", "milestone", "rendered", "executed",
                 "resource_ids", "requirements", "resources")
PROOF_FIELDS = ("requirement", "acceptance_evidence_needed", "constraints", "dependencies")
RESOURCE_PROOF_FIELDS = ("replacement", "behavior", "artwork", "bitmap_exceptions")
ROW_ACCEPTANCE_FIELDS = ("status", "current_evidence", "acceptance_evidence", "remaining")
RESOURCE_ACCEPTANCE_FIELDS = ("translation_status", "replacement", "family_reviewed", "bitmap_exceptions",
                              "behavior_evidence", "visual_evidence")
ACCEPTED = "accepted"
# The transition's own top-level writes; everything else in the register keeps
# its baseline value.
TRANSITION_FIELDS = ("date", "status", "baseline", "normative_sources", "evidence", "requirements")
REVISION = re.compile(r"[0-9a-f]{40}")
DIGEST = re.compile(r"[0-9a-f]{64}")


class Repository:
    """Read-only Git and file access for one audited project root."""

    def __init__(self, audit, root, baseline):
        self.audit, self.root = audit, root
        self.baseline = self.git("rev-parse", "--verify", baseline + "^{commit}").decode("ascii").strip()
        self.records, self.problems, self.commits, self.ancestors = {}, {}, set(), {}
        self.current_problems = {}

    def git(self, *items, check=True, stdin=None):
        result = subprocess.run(["git", "-c", "core.safecrlf=false", *items], cwd=self.root, input=stdin,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if check and result.returncode:
            raise subprocess.CalledProcessError(result.returncode, ["git", *items], result.stdout, result.stderr)
        return result.stdout if check else result

    def commit(self, value, label):
        self.audit.require(isinstance(value, str) and REVISION.fullmatch(value) is not None, f"{label} is not a full commit ID")
        if value not in self.commits:
            self.audit.require(self.git("cat-file", "-e", value + "^{commit}", check=False).returncode == 0, f"{label} is not a commit")
            self.commits.add(value)
        return value

    def in_history(self, revision):
        if revision not in self.ancestors:
            self.ancestors[revision] = self.git("merge-base", "--is-ancestor", revision, self.baseline, check=False).returncode == 0
        return self.ancestors[revision]

    def contents_at(self, revision, paths):
        """Each path's bytes at a revision (None when absent), in one Git call."""
        paths = list(paths)
        output = self.git("cat-file", "--batch", stdin="".join(f"{revision}:{path}\n" for path in paths).encode("utf-8"))
        contents, offset = {}, 0
        for path in paths:
            end = output.index(b"\n", offset)
            header = output[offset:end].split()
            if header[-1] == b"missing" or len(header) != 3 or header[1] != b"blob":
                contents[path], offset = None, end + 1
                continue
            size = int(header[2])
            contents[path], offset = output[end + 1:end + 1 + size], end + 2 + size
        return contents

    def path(self, value, label):
        self.audit.require(isinstance(value, str) and all(ord(char) >= 32 and ord(char) != 127 for char in value),
                           f"Invalid acceptance evidence path: {label}")
        return self.audit.archive_path(self.root, value)

    def text_digests(self, data):
        lf = data.replace(b"\r\n", b"\n")
        return {hashlib.sha256(variant).hexdigest() for variant in (data, lf, lf.replace(b"\n", b"\r\n"))}



def require_text(audit, value, label):
    audit.require(audit.clean_text(value), f"Missing or invalid {label}")


def require_digest(audit, value, label):
    audit.require(isinstance(value, str) and DIGEST.fullmatch(value) is not None, f"Invalid SHA-256: {label}")


def successors(register):
    return {record["superseded"]: record["successors"] for record in register.get("supersessions", [])}


def resolve(chain, name, seen=()):
    """A superseded dependency resolves to the requirements that replace it."""
    if name not in chain:
        return [name]
    return [leaf for item in chain[name] for leaf in resolve(chain, item, seen + (name,))]


def load_record(repo, register, name):
    """Read one acceptance record named by the register; checks its binding."""
    if name in repo.records:
        return repo.records[name]
    audit = repo.audit
    entry = register["evidence"].get(name)
    audit.require(isinstance(entry, dict), f"Unknown acceptance record: {name}")
    audit.require(entry.get("kind") == RECORD_KIND and set(entry) == {"kind", "path", "sha256", "scope"},
                  f"Not an acceptance record: {name}")
    path_text = entry["path"]
    audit.require(isinstance(path_text, str) and path_text.startswith(RECORD_PREFIX) and path_text.endswith(".json"),
                  f"Acceptance records live under {RECORD_PREFIX}: {name}")
    path = repo.path(path_text, name)
    audit.require(path.is_file(), f"Missing acceptance record: {name}")
    audit.require(entry["sha256"] in audit.line_ending_digests(path), f"Stale acceptance record binding: {name}")
    committed = repo.contents_at("HEAD", [path_text])[path_text]
    audit.require(committed is not None and entry["sha256"] in repo.text_digests(committed),
                  f"Acceptance record must be committed with its current bytes: {name}")
    require_text(audit, entry["scope"], f"scope: {name}")
    record = audit.read_json(path.read_text(encoding="utf-8"))
    policy = register["evidence_policy"]["per_acceptance_record_required"]
    audit.require(isinstance(record, dict) and set(record) == set(RECORD_FIELDS) | set(policy),
                  f"Acceptance record schema differs: {name}")
    audit.require(type(record["acceptance_schema"]) is int and record["acceptance_schema"] == RECORD_SCHEMA,
                  f"Unsupported acceptance record schema: {name}")
    audit.require(record["id"] == name, f"Acceptance record identity differs from its register key: {name}")
    audit.require(record["scope"] == entry["scope"], f"Acceptance record scope differs from the register: {name}")
    for field in ("requirement_ids", "resource_ids"):
        values = record[field]
        audit.require(isinstance(values, list) and len(values) == len(set(values)) and
                      all(isinstance(value, str) for value in values), f"Invalid {field}: {name}")
    audit.require(bool(record["requirement_ids"]) or bool(record["resource_ids"]), f"Acceptance record accepts nothing: {name}")
    audit.require(isinstance(record["requirements"], dict) and set(record["requirements"]) == set(record["requirement_ids"]),
                  f"Every accepted requirement needs exactly one proof: {name}")
    audit.require(isinstance(record["resources"], dict) and set(record["resources"]) == set(record["resource_ids"]),
                  f"Every accepted resource needs exactly one proof: {name}")
    require_text(audit, record["prepared_by"], f"author: {name}")
    review = record["reviewer_or_review_outcome"]
    audit.require(isinstance(review, dict) and set(review) == {"reviewer", "outcome", "date"},
                  f"Invalid review outcome: {name}")
    require_text(audit, review["reviewer"], f"reviewer: {name}")
    audit.require(review["outcome"] == ACCEPTED, f"Acceptance record was not accepted in review: {name}")
    audit.require(isinstance(review["date"], str) and date.fromisoformat(review["date"]).isoformat() == review["date"],
                  f"Invalid review date: {name}")
    audit.require(review["reviewer"].strip().casefold() != record["prepared_by"].strip().casefold(),
                  f"Acceptance needs a review separate from its author: {name}")
    audit.require(record["result"] == "passed", f"Acceptance record did not pass: {name}")
    for field in ("scope", "limitations"):
        require_text(audit, record[field], f"{field}: {name}")
    operations = record["exact_commands_or_operations"]
    audit.require(isinstance(operations, list) and bool(operations) and all(audit.clean_text(item) for item in operations),
                  f"Acceptance record lacks exact commands or operations: {name}")
    for flag, fields in (("executed", ("binary_hashes_if_executed", "build_options_if_executed")),
                         ("rendered", ("host_and_backend_if_rendered", "log_and_image_hashes_if_rendered"))):
        audit.require(type(record[flag]) is bool, f"Invalid {flag} flag: {name}")
        for field in fields:
            value = record[field]
            audit.require(isinstance(value, dict) and bool(value) == record[flag],
                          f"{field} must be present exactly when {flag}: {name}")
    for field in ("build_options_if_executed", "host_and_backend_if_rendered"):
        for key, value in record[field].items():
            audit.require(audit.clean_text(key) and (audit.clean_text(value) or
                          (isinstance(value, list) and bool(value) and all(audit.clean_text(item) for item in value))),
                          f"Invalid {field} entry: {name}")
    audit.require(isinstance(record["source_or_asset_hashes"], dict) and bool(record["source_or_asset_hashes"]),
                  f"Acceptance record binds no sources: {name}")
    for field in ("source_or_asset_hashes", "binary_hashes_if_executed", "log_and_image_hashes_if_rendered"):
        for key, value in record[field].items():
            require_digest(audit, value, f"{name}.{field}.{key}")
    audit.require(not (set(record["source_or_asset_hashes"]) & (set(record["binary_hashes_if_executed"]) |
                  set(record["log_and_image_hashes_if_rendered"]))), f"An artifact is bound twice: {name}")
    audit.require(record["milestone"] is None or record["milestone"] in register["milestones"], f"Unknown record milestone: {name}")
    repo.records[name] = record
    return record


def stale_sources(repo, name, record):
    """Problems that make a record no longer describe the present files."""
    if name in repo.problems:
        return repo.problems[name]
    problems = []
    revision = record["engine_revision"]
    repo.commit(revision, f"{name}.engine_revision")
    repo.commit(record["companion_revision"], f"{name}.companion_revision")
    if not repo.in_history(revision):
        problems.append(f"{name}: engine revision {revision} is not in the audited history")
    sources = sorted(record["source_or_asset_hashes"].items())
    paths = {value: repo.path(value, f"{name}.source_or_asset_hashes") for value, _ in sources}
    contents = repo.contents_at(revision, paths)
    for value, recorded in sources:
        path, then = paths[value], contents[value]
        if then is None or recorded not in repo.text_digests(then):
            problems.append(f"{name}: {value} hash does not describe engine revision {revision}")
        elif not path.is_file() or recorded not in repo.audit.line_ending_digests(path):
            problems.append(f"{name}: {value} changed after engine revision {revision}")
    repo.problems[name] = problems
    return problems


def check_artifacts(repo, name, record):
    """Executed and rendered artifacts must exist with their recorded bytes."""
    for field in ("binary_hashes_if_executed", "log_and_image_hashes_if_rendered"):
        for value, recorded in sorted(record[field].items()):
            path = repo.path(value, f"{name}.{field}")
            repo.audit.require(path.is_file(), f"Missing executed/rendered artifact: {name}: {value}")
            repo.audit.require(repo.audit.digest(path) == recorded, f"Artifact hash mismatch: {name}: {value}")


def rendered_image(repo, value):
    """Recognize the bounded headers of engine screenshot formats.

    This identifies image evidence rather than treating a log as artwork. It
    does not decode pixels or establish that the image proves a visual claim.
    """
    path = repo.path(value, "rendered image")
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        header = stream.read(32)
    if path.suffix.lower() == ".png":
        return (len(header) >= 29 and header[:16] == b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" and
                all(struct.unpack(">II", header[16:24])))
    if path.suffix.lower() == ".tga":
        if len(header) < 18:
            return False
        width, height = struct.unpack("<HH", header[12:16])
        # Engine captures are true-color or grayscale; indexed artwork is
        # not a screenshot produced by the registered screenshot command.
        return (header[1] == 0 and header[2] in (2, 3, 10, 11) and width > 0 and height > 0 and
                header[16] in (8, 24, 32) and path.stat().st_size > 18 + header[0])
    if path.suffix.lower() in (".jpg", ".jpeg"):
        return len(header) >= 4 and header[:3] == b"\xff\xd8\xff"
    return False


def current_record_problems(repo, name, record):
    """Earlier acceptance needs its sources and executed/rendered artifacts."""
    if name in repo.current_problems:
        return repo.current_problems[name]
    problems = list(stale_sources(repo, name, record))
    for field in ("binary_hashes_if_executed", "log_and_image_hashes_if_rendered"):
        for value, recorded in record[field].items():
            path = repo.path(value, f"{name}.{field}")
            if not path.is_file() or repo.audit.digest(path) != recorded:
                problems.append(f"{name}: executed/rendered artifact changed or missing: {value}")
    repo.current_problems[name] = problems
    return problems


def references(record):
    return (set(record["source_or_asset_hashes"]) | set(record["binary_hashes_if_executed"]) |
            set(record["log_and_image_hashes_if_rendered"]) |
            {f"operation:{index}" for index in range(len(record["exact_commands_or_operations"]))})


def check_proof(audit, record, proof, label):
    audit.require(isinstance(proof, dict) and proof.get("result") == "passed", f"Unproven or failed: {label}")
    evidence = proof.get("evidence")
    audit.require(isinstance(evidence, list) and bool(evidence) and len(evidence) == len(set(evidence)) and
                  set(evidence) <= references(record), f"Proof cites no recorded evidence: {label}")


def check_requirement(repo, register, rows, chain, name, record, req):
    audit, row = repo.audit, rows[req]
    proof = record["requirements"][req]
    audit.require(isinstance(proof, dict) and tuple(proof) == PROOF_FIELDS, f"Requirement proof schema differs: {name}.{req}")
    audit.require(proof["requirement"] == row["requirement"], f"Acceptance record proves different requirement text: {name}.{req}")
    needed = row["acceptance_evidence_needed"]
    items = proof["acceptance_evidence_needed"]
    audit.require(isinstance(items, list) and [item.get("need") if isinstance(item, dict) else None for item in items] == needed,
                  f"Acceptance record leaves conjuncts unproven: {name}.{req}")
    for index, item in enumerate(items):
        audit.require(set(item) == {"need", "result", "evidence"}, f"Conjunct proof schema differs: {name}.{req}[{index}]")
        check_proof(audit, record, item, f"{name}.{req} conjunct {index + 1}")
    constraints = row.get("constraints", {})
    audit.require(isinstance(proof["constraints"], dict) and set(proof["constraints"]) == set(constraints),
                  f"Acceptance record leaves constraints unproven: {name}.{req}")
    for key, item in proof["constraints"].items():
        audit.require(set(item) == {"result", "evidence"}, f"Constraint proof schema differs: {name}.{req}.{key}")
        check_proof(audit, record, item, f"{name}.{req} constraint {key}")
    bound = set(record["source_or_asset_hashes"])
    for ref in row["source_refs"]:
        source = register["normative_sources"][ref.split(":", 1)[0]]["path"]
        audit.require(source in bound, f"Acceptance record does not bind normative source {source}: {name}.{req}")

    # Dependencies resolve through supersessions; each must be proved.
    resolved = []
    for dep in row["dependencies"]:
        resolved.extend(item for item in resolve(chain, dep) if item not in resolved)
    deps = proof["dependencies"]
    audit.require(isinstance(deps, dict) and set(deps) == set(resolved), f"Acceptance record does not cover dependencies exactly: {name}.{req}")
    gate = req.startswith("GATE-")
    bounded = []
    for dep in resolved:
        entry, dep_row = deps[dep], rows[dep]
        if dep_row["status"] == "verified":
            audit.require(entry == {"verified_by": dep_row["acceptance_evidence"]},
                          f"Dependency proof must cite its acceptance records: {name}.{req}.{dep}")
            for cited in dep_row["acceptance_evidence"]:
                problems = current_record_problems(repo, cited, load_record(repo, register, cited))
                audit.require(not problems, f"Dependency acceptance is stale: {req} needs {dep}: " + "; ".join(problems))
        elif gate and dep_row["status"] == "partial" and not dep.startswith(("GATE-", "FINAL-")):
            # A milestone gate accepts its bounded exit scope; the dependency
            # stays partial until its full requirement passes.
            audit.require(isinstance(entry, dict) and set(entry) == {"milestone", "scope", "result", "evidence"},
                          f"Missing dependency acceptance: {req} needs {dep}")
            audit.require(entry["milestone"] == row["milestone_owner"] == record["milestone"],
                          f"Dependency evidence is not at the accepted gate's milestone: {name}.{req}.{dep}")
            require_text(audit, entry["scope"], f"bounded dependency scope: {name}.{req}.{dep}")
            check_proof(audit, record, entry, f"{name}.{req} dependency {dep}")
            bounded.append(dep)
        else:
            audit.require(False, f"Missing dependency acceptance: {req} needs {dep}")
    if gate:
        audit.require(record["milestone"] == row["milestone_owner"], f"Gate record names a different milestone: {name}.{req}")
    if req.startswith("FINAL-") or req == "GATE-007":
        replaced = set(chain)
        missing = [item for item, other in rows.items() if item not in replaced and
                   not item.startswith("FINAL-") and item != "GATE-007" and other["status"] != "verified"]
        audit.require(not missing, f"Final acceptance needs every applicable requirement verified: {req}: " + ", ".join(missing[:8]))
    return bounded


def check_resource(repo, register, manifest, name, record, resource_id):
    audit = repo.audit
    proof = record["resources"][resource_id]
    audit.require(isinstance(proof, dict) and tuple(proof) == RESOURCE_PROOF_FIELDS, f"Resource proof schema differs: {name}")
    entry = manifest[resource_id]
    audit.require(proof["replacement"] == entry["replacement"], f"Resource proof names a different replacement: {name}: {resource_id}")
    audit.require(entry["replacement"] in record["source_or_asset_hashes"], f"Resource replacement is not bound: {name}: {resource_id}")
    audit.require(proof["bitmap_exceptions"] == entry["bitmap_exceptions"], f"Resource bitmap exceptions differ from the manifest: {name}")
    for kind in ("behavior", "artwork"):
        items = proof[kind]
        audit.require(isinstance(items, list) and bool(items), f"Unqualified resource: {resource_id} lacks {kind} proof")
        for index, item in enumerate(items):
            audit.require(isinstance(item, dict) and set(item) == {"claim", "result", "evidence"}, f"Resource proof schema differs: {name}.{kind}")
            require_text(audit, item["claim"], f"{kind} claim: {name}")
            check_proof(audit, record, item, f"{name}.{resource_id} {kind} {index + 1}")
            # Artwork is judged from rendered output, never from source alone.
            audit.require(kind != "artwork" or any(rendered_image(repo, value) for value in
                          set(item["evidence"]) & set(record["log_and_image_hashes_if_rendered"])),
                          f"Unqualified resource: {resource_id} artwork proof cites no rendered image")


def check_resource_entry(audit, entry, label):
    """Fields an accepted migration resource must carry."""
    audit.require(entry["translation_status"] == ACCEPTED and entry["family_reviewed"] is True,
                  f"Unqualified resource: {label} is not reviewed and accepted")
    audit.require(isinstance(entry["replacement"], str) and entry["replacement"].endswith(".rui"),
                  f"Unqualified resource: {label} has no retained replacement")
    exceptions = entry["bitmap_exceptions"]
    audit.require(isinstance(exceptions, list) and all(isinstance(item, dict) and set(item) == {"path", "reason", "review"} and
                  all(audit.clean_text(item[key]) for key in item) for item in exceptions),
                  f"Unqualified resource: {label} has unreviewed bitmap exceptions")
    for field in ("behavior_evidence", "visual_evidence"):
        values = entry[field]
        audit.require(isinstance(values, list) and bool(values) and len(values) == len(set(values)),
                      f"Unqualified resource: {label} lacks {field}")


def accepted_record_problems(repo, register, manifest_rows, rows):
    """Staleness of requirements and resources accepted before this audit."""
    problems = {}
    for req, row in rows.items():
        if row["status"] == "verified":
            for name in row["acceptance_evidence"]:
                found = current_record_problems(repo, name, load_record(repo, register, name))
                if found:
                    problems.setdefault(req, []).extend(found)
    for resource_id, entry in manifest_rows.items():
        if entry["translation_status"] == ACCEPTED:
            for name in sorted(set(entry["behavior_evidence"]) | set(entry["visual_evidence"])):
                found = current_record_problems(repo, name, load_record(repo, register, name))
                if found:
                    problems.setdefault(resource_id, []).extend(found)
    return problems


def validate(args, audit) -> dict:
    """Audit one acceptance transition against an immutable baseline commit."""
    audit.require(not args.partial and not args.defer_hashes and not args.withdraw and not args.withdraw_resource,
                  "An acceptance transition cannot change partial status, withdraw acceptance or defer hashes")
    accept, accept_resources = list(args.accept), list(args.accept_resource)
    audit.require(bool(accept) or bool(accept_resources), "Name each requirement or resource the transition accepts")
    audit.require(len(accept) == len(set(accept)) and len(accept_resources) == len(set(accept_resources)),
                  "A requirement or resource is named twice")
    root = args.root.resolve()
    repo = Repository(audit, root, args.baseline)
    register_path = (args.register or root / "docs/dev/ui/product-requirements.json").resolve()
    audit.require(register_path.is_relative_to(root), "Candidate register leaves project")
    current = audit.read_json(register_path.read_text(encoding="utf-8"))
    previous = audit.read_json(repo.git("show", repo.baseline + ":docs/dev/ui/product-requirements.json").decode("utf-8"))

    # The register's contract is unchanged; only acceptance writes differ.
    audit.require(current.keys() == previous.keys(), "Register top-level schema changed")
    for key in previous:
        if key not in TRANSITION_FIELDS:
            audit.require(current[key] == previous[key], f"Protected register contract changed: {key}")
    require_text(audit, current["status"], "register status")
    audit.require(current["date"] >= previous["date"], "Register date moved backwards")
    for key, value in previous["baseline"].items():
        if key not in ("engine_revision", "translated_or_accepted_resources"):
            audit.require(current["baseline"].get(key) == value, f"Protected baseline field changed: {key}")
    audit.require(current["baseline"].keys() == previous["baseline"].keys(), "Baseline schema changed")
    audit.require(current["baseline"]["engine_revision"] == repo.baseline, "Current transition must identify the audited baseline revision")
    for name, source in previous["normative_sources"].items():
        audit.require({k: v for k, v in source.items() if k != "sha256"} ==
                      {k: v for k, v in current["normative_sources"].get(name, {}).items() if k != "sha256"},
                      f"Normative source authority/path changed: {name}")
    audit.require(current["normative_sources"].keys() == previous["normative_sources"].keys(), "Normative source set changed")
    for name, item in previous["evidence"].items():
        audit.require(name in current["evidence"], f"Historical evidence removed: {name}")
        audit.require({k: v for k, v in item.items() if k != "sha256"} ==
                      {k: v for k, v in current["evidence"][name].items() if k != "sha256"},
                      f"Historical evidence scope/result/binding changed: {name}")
    added = [name for name in current["evidence"] if name not in previous["evidence"]]

    # Requirements: identity and order kept; only the named rows change.
    before = {row["id"]: row for row in previous["requirements"]}
    rows = {row["id"]: row for row in current["requirements"]}
    audit.require([row["id"] for row in current["requirements"]] == [row["id"] for row in previous["requirements"]] and
                  len(rows) == len(before), "Requirement order/identity changed")
    chain = successors(current)
    audit.require(set(accept) <= rows.keys(), "Unknown --accept requirement")
    for name, row in rows.items():
        old = before[name]
        if name not in accept:
            audit.require(row == old, f"Only the named requirements may change: {name}")
            continue
        audit.require(name not in chain, f"A superseded requirement cannot be accepted: {name}")
        audit.require(old["status"] in ("pending", "partial") and row["status"] == "verified",
                      f"Acceptance must move an unaccepted requirement to verified: {name}")
        audit.require(row.keys() == old.keys(), f"Requirement schema changed: {name}")
        for field, value in row.items():
            if field not in ROW_ACCEPTANCE_FIELDS:
                audit.require(value == old[field], f"Normative scope/constraints changed: {name}.{field}")
        audit.require(set(old["current_evidence"]) <= set(row["current_evidence"]) and
                      len(row["current_evidence"]) == len(set(row["current_evidence"])), f"Historical requirement evidence removed: {name}")
        cited = row["acceptance_evidence"]
        audit.require(bool(cited) and len(cited) == len(set(cited)) and set(cited) <= set(added),
                      f"Acceptance must cite acceptance records added by this transition: {name}")
        require_text(audit, row["remaining"], f"remaining text: {name}")
    audit.require_acyclic(rows)

    # Migration resources: only the named resources change, to accepted.
    manifest_path = root / "docs/dev/ui/migration-manifest.json"
    manifest = audit.read_json(manifest_path.read_text(encoding="utf-8"))
    old_manifest = audit.read_json(repo.git("show", repo.baseline + ":docs/dev/ui/migration-manifest.json").decode("utf-8"))
    audit.require(manifest.keys() == old_manifest.keys() and
                  all(manifest[key] == old_manifest[key] for key in manifest if key != "resources"),
                  "Migration inventory records changed")
    resources, old_resources = manifest["resources"], old_manifest["resources"]
    audit.require([item["resource_id"] for item in resources] == [item["resource_id"] for item in old_resources],
                  "Migration resource identity/order changed")
    by_id = {item["resource_id"]: item for item in resources}
    audit.require(len(by_id) == len(resources) == current["baseline"]["effective_gui_count"], "Migration inventory denominator changed")
    audit.require(set(accept_resources) <= by_id.keys(), "Unknown --accept-resource resource")
    for item, old in zip(resources, old_resources):
        label = item["resource_id"]
        if label not in accept_resources:
            audit.require(item == old, f"Only the named resources may change: {label}")
            continue
        audit.require(old["translation_status"] == "pending", f"Resource is already accepted: {label}")
        audit.require(item.keys() == old.keys() and all(item[key] == old[key] for key in item if key not in RESOURCE_ACCEPTANCE_FIELDS),
                      f"Resource source identity changed: {label}")
        check_resource_entry(audit, item, label)
        repo.path(item["replacement"], f"{label}.replacement")
        for field in ("behavior_evidence", "visual_evidence"):
            audit.require(set(item[field]) <= set(added), f"Resource acceptance must cite records added by this transition: {label}")
    accepted_count = sum(item["translation_status"] == ACCEPTED for item in resources)
    audit.require(current["baseline"]["translated_or_accepted_resources"] == accepted_count,
                  "Baseline accepted-resource count differs from the manifest")

    # Records: every new one is an acceptance record that this transition uses
    # in full, and no record claims beyond the transition.
    citing = {}
    for req in accept:
        for name in rows[req]["acceptance_evidence"]:
            citing.setdefault(name, {"requirements": set(), "resources": set()})["requirements"].add(req)
    for resource_id in accept_resources:
        for field in ("behavior_evidence", "visual_evidence"):
            for name in by_id[resource_id][field]:
                citing.setdefault(name, {"requirements": set(), "resources": set()})["resources"].add(resource_id)
    audit.require(set(added) == set(citing), "Every added evidence record must be an acceptance record this transition cites")
    report_records, bounded = {}, {}
    for name in added:
        record = load_record(repo, current, name)
        audit.require(set(record["requirement_ids"]) == citing[name]["requirements"] and
                      set(record["resource_ids"]) == citing[name]["resources"],
                      f"Acceptance record claims beyond what this transition accepts: {name}")
        problems = stale_sources(repo, name, record)
        audit.require(not problems, "Stale acceptance evidence: " + "; ".join(problems))
        check_artifacts(repo, name, record)
        gates = [req for req in record["requirement_ids"] if req.startswith("GATE-")]
        audit.require(not gates or record["milestone"] is not None, f"A gate record must name its milestone: {name}")
        audit.require(gates or record["milestone"] is None, f"Only a gate record names a milestone scope: {name}")
        for req in record["requirement_ids"]:
            found = check_requirement(repo, current, rows, chain, name, record, req)
            if found:
                bounded.setdefault(req, []).extend(found)
        for resource_id in record["resource_ids"]:
            check_resource(repo, current, by_id, name, record, resource_id)
        report_records[name] = {"path": current["evidence"][name]["path"], "sha256": current["evidence"][name]["sha256"],
                                "engine_revision": record["engine_revision"], "milestone": record["milestone"],
                                "requirement_ids": record["requirement_ids"], "resource_ids": record["resource_ids"],
                                "reviewer": record["reviewer_or_review_outcome"]["reviewer"]}
    for resource_id in accept_resources:
        audit.require(any(name in by_id[resource_id]["visual_evidence"] and resource_id in repo.records[name]["resource_ids"]
                          for name in added) and
                      any(name in by_id[resource_id]["behavior_evidence"] and resource_id in repo.records[name]["resource_ids"]
                          for name in added), f"Unqualified resource: {resource_id} lacks behavior or artwork records")
    for req in accept:
        if req.startswith("FINAL-") or req == "GATE-007":
            unaccepted = [item["resource_id"] for item in resources if item["translation_status"] != ACCEPTED]
            audit.require(not unaccepted, f"Final acceptance needs every migration resource accepted: {req}: " +
                          ", ".join(unaccepted[:4]))

    # Earlier acceptance must still describe the present files.
    stale = accepted_record_problems(repo, current, {key: value for key, value in by_id.items() if key not in accept_resources},
                                     {key: value for key, value in rows.items() if key not in accept})
    audit.require(not stale, "Earlier acceptance is stale: " + "; ".join(f"{key}: {', '.join(value)}" for key, value in stale.items()))
    files, deferred, historical_unbound = audit.check_source_bindings(root, current, previous, False)
    markdown_path = root / "docs/dev/ui/product-requirements.md"
    links, counts = audit.check_markdown(root, markdown_path, rows, current["normative_sources"]["product"]["path"])
    finals = [name for name in rows if name.startswith("FINAL-")]
    return {
        "schema_version": 1, "status": "acceptance_transition_passed",
        "product_acceptance": all(rows[name]["status"] == "verified" for name in finals),
        "baseline_engine_revision": repo.baseline,
        "register_sha256": audit.digest(register_path), "markdown_sha256": audit.digest(markdown_path),
        "migration_manifest_sha256": audit.digest(manifest_path),
        "validator_sha256": audit.digest(Path(audit.__file__).resolve()), "transition_sha256": audit.digest(Path(__file__).resolve()),
        "accepted_requirements": accept, "accepted_resources": accept_resources,
        "acceptance_records": report_records, "gate_bounded_dependencies": bounded,
        "status_counts": dict(counts), "accepted_migration_entries": accepted_count,
        "migration_entries": len(resources), "earlier_acceptance_current": True,
        "source_hashes": files, "historical_unbound_references": historical_unbound, "local_links_checked": links,
        "limitations": "Audits the recorded acceptance claims against the present files and the audited history. "
                       "It does not rerun builds, tests or captures, and cannot judge whether a review was competent; "
                       "it requires each named claim to be proved item by item and reviewed by someone other than its author."
    }
