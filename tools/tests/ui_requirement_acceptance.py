#!/usr/bin/env python3
"""Exercise the UI register's acceptance transition (roadmap UI-11) on real Git fixtures.

Only disposable project-local repositories are created. A passing transition
proves that acceptance records are complete and current, item by item; these
checks qualify that audit, not any UI implementation.
"""
from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import importlib.util
import json
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ui_requirement_validator", ROOT / "tools/ui/validate_product_requirements.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)
TRANSITION = VALIDATOR.load_sibling("acceptance_transition")
POLICY = ["requirement_ids", "scope", "engine_revision", "companion_revision", "source_or_asset_hashes",
          "binary_hashes_if_executed", "build_options_if_executed", "exact_commands_or_operations",
          "host_and_backend_if_rendered", "log_and_image_hashes_if_rendered", "result",
          "reviewer_or_review_outcome", "limitations"]
PRODUCT, VISUAL = "docs/dev/plans/ui-product.md", "docs/dev/ui-visual-design.md"
WIDGET, MENU, HUD = "src/ui/Widget.cpp", "content/guis/menu.rui", "content/guis/hud.rui"


def requirement(name, milestone, dependencies=(), status="partial", needs=("Behavior evidence", "Visual evidence"),
                constraints=None):
    row = {"id": name, "title": name + " title", "milestone_owner": milestone, "implementation_owner": "UI",
           "source_refs": ["product:2", "visual:7"], "requirement": f"{name} works completely.",
           "dependencies": list(dependencies), "acceptance_evidence_needed": list(needs), "status": status,
           "current_evidence": ["foundation"] if status != "pending" else [], "acceptance_evidence": [],
           "remaining": "Complete and qualify the requirement."}
    if constraints is not None:
        row["constraints"] = constraints
    return row


class AcceptanceTransitionTests(unittest.TestCase):
    STATE = ("base", "baseline", "previous", "manifest", "current", "current_manifest")
    template = None

    def setUp(self):
        self.temp_parent = VALIDATOR.archive_path(ROOT.resolve(), ".tmp/ui-requirement-acceptance-tests")
        self.temp_parent.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=self.temp_parent)).resolve()
        self.addCleanup(self.clean_fixture, self.root)
        # Git is slow to start on Windows: build the repository once, copy it per test.
        cls = type(self)
        if cls.template is None:
            self.build_fixture()
            template = Path(tempfile.mkdtemp(prefix="template-", dir=self.temp_parent)).resolve()
            shutil.copytree(self.root, template, dirs_exist_ok=True)
            cls.template = (template, copy.deepcopy({key: getattr(self, key) for key in self.STATE}))
        else:
            shutil.copytree(cls.template[0], self.root, dirs_exist_ok=True)
            for key, value in copy.deepcopy(cls.template[1]).items():
                setattr(self, key, value)

    @classmethod
    def tearDownClass(cls):
        if cls.template is not None:
            cls.clean_fixture(cls, cls.template[0])
            cls.template = None

    def build_fixture(self):
        self.write(PRODUCT, "# Product scope\n\nEvery requirement is mandatory.\n")
        self.write(VISUAL, "# Visual contract\n\n[Scope](plans/ui-product.md).\n")
        self.write("docs/dev/ui/value-controls.md", "# Value controls\n")
        self.write("docs/dev/ui/foundation.md", "# Foundation\n\nLimited implementation.\n")
        self.write(WIDGET, "int Widget() { return 1; }\n")
        self.write(MENU, '{"format": "retained-ui", "id": "menu"}\n')
        self.write(HUD, '{"format": "retained-ui", "id": "hud"}\n')
        self.write(".gitignore", ".tmp/\n")
        rows = [
            requirement("DOC-001", "M1"),
            requirement("WID-001", "M2", ["DOC-001"], constraints={"target_dp": 44}),
            requirement("ART-003", "M2"),
            requirement("ART-018", "M2"),
            requirement("WID-002", "M2", ["ART-003"]),
            requirement("FLOW-002", "M2"),
            requirement("ED-029", "M2"),
            requirement("RUN-001", "M1", status="pending"),
            requirement("GATE-003", "M2", ["FLOW-002", "ED-029"], status="pending",
                        needs=["Current direct evidence proves every stated exit condition at this milestone scope."]),
            requirement("GATE-002", "M1", ["RUN-001"], status="pending", needs=["Exit evidence."]),
            requirement("FINAL-001", "M6", ["GATE-003"], status="pending", needs=["Final independent audit."]),
            *(requirement(f"FINAL-00{i}", "M6", status="pending", needs=["Final independent audit."]) for i in range(2, 8)),
            requirement("GATE-007", "M6", ["FINAL-001"], status="pending", needs=["Cutover evidence."]),
        ]
        self.previous = {
            "schema_version": 19, "id_namespace": "openq4.ui.product", "date": "2026-10-23",
            "status": "Implementation incomplete.",
            "baseline": {"engine_revision": "fixture", "companion_revision": "fixture", "effective_gui_count": 2,
                         "translated_or_accepted_resources": 0},
            "normative_sources": {
                "product": {"path": PRODUCT, "sha256": self.hash(PRODUCT), "authority": "completion scope"},
                "visual": {"path": VISUAL, "sha256": self.hash(VISUAL), "authority": "visual contract"}},
            "milestones": {f"M{i}": f"Milestone {i}" for i in range(7)},
            "schema_contract": {"status_values": {"pending": "-", "partial": "-", "verified": "-"}},
            "evidence_policy": {"per_acceptance_record_required": POLICY, "constraints": ["Acceptance is item by item."]},
            "evidence": {"foundation": {"kind": "implementation_checkpoint", "path": "docs/dev/ui/foundation.md",
                                        "sha256": self.hash("docs/dev/ui/foundation.md"), "scope": "Limited foundation."}},
            "groups": {group: {"title": group, "implementation_owner": "UI", "source_refs": ["product:2"]}
                       for group in ("DOC", "WID", "ART", "FLOW", "ED", "RUN", "GATE", "FINAL")},
            "audit_findings": {f"UI-{i:02}": [] for i in range(1, 13)},
            "supersessions": [{"superseded": "ART-003", "successors": ["ART-018"], "normative_change": "Measured again.",
                               "source_refs": ["visual:4"], "carried_evidence": []}],
            "requirements": rows, "planning_authority": "docs/dev/roadmap.md"}
        self.manifest = {"schema_version": 2, "status": "Inventory.", "resources": [
            self.resource("guis/menu.gui"), self.resource("guis/hud.gui")]}
        self.save_register(self.previous)
        self.write_json("docs/dev/ui/migration-manifest.json", self.manifest)
        self.git("init", "--quiet")
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Baseline fixture")
        self.base = self.head()
        # Every audited register names the commit it is audited against.
        self.previous["baseline"]["engine_revision"] = self.base
        self.previous["baseline"]["companion_revision"] = self.base
        self.commit_register(self.previous, "Register names its baseline")
        self.current = copy.deepcopy(self.previous)
        self.current_manifest = copy.deepcopy(self.manifest)
        self.current["baseline"]["engine_revision"] = self.baseline
        self.write(".tmp/bin/openQ4-client_x64.exe", "client bytes")
        self.write(".tmp/run/openq4.log", "UI ready\n")
        (self.root / ".tmp/run/menu.png").write_bytes(base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="))

    # Fixture plumbing ---------------------------------------------------------
    def clean_fixture(self, target):
        target = target.resolve()
        if not target.exists():
            return
        expected = VALIDATOR.archive_path(ROOT.resolve(), ".tmp/ui-requirement-acceptance-tests")
        if target.parent != expected or not target.name.startswith(("case-", "template-")):
            raise RuntimeError(f"Unsafe test cleanup target: {target}")
        paths = (target, *target.rglob("*"))
        for path in paths:
            reparse = getattr(path.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if path.is_symlink() or reparse:
                raise RuntimeError(f"Refusing cleanup through a link: {path}")
        for path in paths:
            if path.is_file():
                path.chmod(path.stat().st_mode | stat.S_IWUSR)
        shutil.rmtree(target)

    def git(self, *args):
        return subprocess.check_output(["git", "-c", "core.autocrlf=false",
            "-c", "core.hooksPath=" + str(self.root / ".no-hooks"),
            "-c", "user.name=Acceptance test", "-c", "user.email=acceptance-test@example.invalid",
            "-c", "commit.gpgsign=false", *args], cwd=self.root, stderr=subprocess.PIPE)

    def head(self):
        return self.git("rev-parse", "HEAD").decode("ascii").strip()

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")

    def write_json(self, path, value):
        self.write(path, json.dumps(value, indent=2) + "\n")

    def hash(self, path):
        return hashlib.sha256((self.root / path).read_bytes()).hexdigest()

    def resource(self, path):
        closure = hashlib.sha256(path.encode()).hexdigest()
        return {"path": path, "family_hint": "marine", "family_reviewed": False, "replacement": None,
                "bitmap_exceptions": None, "translation_status": "pending", "behavior_evidence": [],
                "visual_evidence": [], "resource_id": f"{path}@{closure}", "source": "retail", "source_sha256": closure,
                "source_closure_sha256": closure, "includes": [], "source_diagnostics": [], "configurations": ["base"]}

    def save_register(self, register):
        counts = {status: sum(row["status"] == status for row in register["requirements"])
                  for status in ("partial", "pending", "verified")}
        verified = "one" if counts["verified"] == 1 else str(counts["verified"])
        self.write("docs/dev/ui/product-requirements.md", "# Requirements\n\nThere are "
                   f"{counts['partial']} partial, {counts['pending']} pending and {verified} verified requirements.\n")
        self.write_json("docs/dev/ui/product-requirements.json", register)

    def commit_register(self, register, message):
        self.save_register(register)
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", message)
        self.baseline = self.head()

    def rows(self, register=None):
        return {row["id"]: row for row in (register or self.current)["requirements"]}

    def resource_id(self, index):
        return self.current_manifest["resources"][index]["resource_id"]

    # Acceptance records -------------------------------------------------------
    def record(self, name, requirements=(), resources=(), milestone=None, revision=None):
        rows = self.rows()
        sources = {path: self.hash(path) for path in (PRODUCT, VISUAL, WIDGET, MENU, HUD)}
        record = {"acceptance_schema": 1, "id": name, "prepared_by": "UI implementer", "milestone": milestone,
                  "rendered": True, "executed": True, "requirement_ids": list(requirements), "resource_ids": list(resources),
                  "scope": f"{name} complete scope.", "engine_revision": revision or self.base,
                  "companion_revision": revision or self.base, "source_or_asset_hashes": sources,
                  "binary_hashes_if_executed": {".tmp/bin/openQ4-client_x64.exe": self.hash(".tmp/bin/openQ4-client_x64.exe")},
                  "build_options_if_executed": {"buildtype": "debugoptimized"},
                  "exact_commands_or_operations": ["python tools/ui/capture.py --renderer gl"],
                  "host_and_backend_if_rendered": {"host": "Windows 11 x64", "backends": ["gl", "vulkan"]},
                  "log_and_image_hashes_if_rendered": {".tmp/run/openq4.log": self.hash(".tmp/run/openq4.log"),
                                                       ".tmp/run/menu.png": self.hash(".tmp/run/menu.png")},
                  "result": "passed",
                  "reviewer_or_review_outcome": {"reviewer": "Independent reviewer", "outcome": "accepted", "date": "2026-10-23"},
                  "limitations": "Windows only.", "requirements": {}, "resources": {}}
        for req in requirements:
            row = rows[req]
            deps = {}
            for dep in row["dependencies"]:
                for leaf in TRANSITION.resolve(TRANSITION.successors(self.current), dep):
                    leaf_row = rows[leaf]
                    deps[leaf] = ({"verified_by": leaf_row["acceptance_evidence"]} if leaf_row["status"] == "verified" else
                                  {"milestone": milestone, "scope": f"{leaf} at the gate's scope.", "result": "passed",
                                   "evidence": ["operation:0", ".tmp/run/openq4.log"]})
            record["requirements"][req] = {
                "requirement": row["requirement"],
                "acceptance_evidence_needed": [{"need": need, "result": "passed", "evidence": [WIDGET, ".tmp/run/menu.png"]}
                                               for need in row["acceptance_evidence_needed"]],
                "constraints": {key: {"result": "passed", "evidence": [".tmp/run/menu.png"]} for key in row.get("constraints", {})},
                "dependencies": deps}
        for resource_id in resources:
            entry = next(item for item in self.current_manifest["resources"] if item["resource_id"] == resource_id)
            record["resources"][resource_id] = {
                "replacement": entry["replacement"],
                "behavior": [{"claim": "Every event and command works.", "result": "passed", "evidence": ["operation:0"]}],
                "artwork": [{"claim": "Matches the reviewed family art.", "result": "passed", "evidence": [".tmp/run/menu.png"]}],
                "bitmap_exceptions": entry["bitmap_exceptions"]}
        return record

    def publish(self, record, commit=True):
        path = f"docs/dev/ui/acceptance/{record['id']}.json"
        self.write_json(path, record)
        self.current["evidence"][record["id"]] = {"kind": "acceptance_record", "path": path,
                                                  "sha256": self.hash(path), "scope": record["scope"]}
        if commit:
            self.git("add", "--", path)
            self.git("commit", "--quiet", "--allow-empty", "-m", "Record reviewed fixture evidence")

    def mark(self, records, *names):
        for row in self.current["requirements"]:
            if row["id"] in names:
                row["status"] = "verified"
                row["acceptance_evidence"] = list(records)
                row["remaining"] = "None at the accepted scope; a stale source withdraws it."

    def mark_resource(self, index, behavior, visual, replacement=MENU, exceptions=()):
        entry = self.current_manifest["resources"][index]
        entry.update(translation_status="accepted", family_reviewed=True, replacement=replacement,
                     bitmap_exceptions=list(exceptions), behavior_evidence=list(behavior), visual_evidence=list(visual))
        self.current["baseline"]["translated_or_accepted_resources"] = sum(
            item["translation_status"] == "accepted" for item in self.current_manifest["resources"])

    def accept(self, name, requirements=(), resources=(), milestone=None, commit=True):
        """Apply one acceptance to the candidate files and publish its complete record."""
        self.mark([name], *requirements)
        for resource_id in resources:
            index = [item["resource_id"] for item in self.current_manifest["resources"]].index(resource_id)
            self.mark_resource(index, [name], [name])
        record = self.record(name, requirements, resources, milestone)
        self.publish(record, commit)
        return record

    def audit(self, accept=(), resources=()):
        self.save_register(self.current)
        self.write_json("docs/dev/ui/migration-manifest.json", self.current_manifest)
        args = argparse.Namespace(root=self.root, baseline=self.baseline, register=None, partial=[], defer_hashes=False,
                                  withdraw=[], withdraw_resource=[], accept=list(accept), accept_resource=list(resources))
        return TRANSITION.validate(args, VALIDATOR.audit_helpers())

    def increment(self, withdraw=(), withdraw_resource=()):
        self.save_register(self.current)
        self.write_json("docs/dev/ui/migration-manifest.json", self.current_manifest)
        args = argparse.Namespace(root=self.root, baseline=self.baseline, register=None, partial=[], defer_hashes=False,
                                  withdraw=list(withdraw), withdraw_resource=list(withdraw_resource), revision=False)
        return VALIDATOR.validate(args)

    def land(self, message="Accept"):
        """Commit the candidate so later audits use it as their baseline."""
        self.save_register(self.current)
        self.write_json("docs/dev/ui/migration-manifest.json", self.current_manifest)
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", message)
        self.baseline = self.head()
        self.previous = copy.deepcopy(self.current)
        self.current["baseline"]["engine_revision"] = self.baseline

    # Accepted transitions -----------------------------------------------------
    def test_accepts_named_requirements_with_complete_current_records(self):
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        report = self.audit(["DOC-001", "WID-001"])
        self.assertEqual(report["status"], "acceptance_transition_passed")
        self.assertFalse(report["product_acceptance"])
        self.assertEqual(report["status_counts"]["verified"], 2)
        self.assertEqual(report["acceptance_records"]["acceptance-widgets"]["requirement_ids"], ["DOC-001", "WID-001"])
        self.assertEqual(report["accepted_migration_entries"], 0)

    def test_dependency_resolves_through_a_supersession(self):
        self.accept("acceptance-art", ["ART-018"])
        self.accept("acceptance-checkbox", ["WID-002"])
        report = self.audit(["ART-018", "WID-002"])
        proof = json.loads((self.root / "docs/dev/ui/acceptance/acceptance-checkbox.json").read_text(encoding="utf-8"))
        self.assertEqual(set(proof["requirements"]["WID-002"]["dependencies"]), {"ART-018"})
        self.assertEqual(report["status_counts"]["verified"], 2)

    def test_gate_accepts_its_bounded_milestone_scope(self):
        self.accept("acceptance-settings-exit", ["GATE-003"], milestone="M2")
        report = self.audit(["GATE-003"])
        self.assertEqual(report["gate_bounded_dependencies"], {"GATE-003": ["FLOW-002", "ED-029"]})
        rows = self.rows()
        self.assertEqual((rows["FLOW-002"]["status"], rows["ED-029"]["status"]), ("partial", "partial"))

    def test_resource_acceptance_needs_behavior_and_artwork(self):
        self.accept("acceptance-menu", resources=[self.resource_id(0)])
        report = self.audit(resources=[self.resource_id(0)])
        self.assertEqual(report["accepted_migration_entries"], 1)
        self.assertEqual(report["accepted_resources"], [self.resource_id(0)])

    def test_final_gate_needs_every_requirement_and_resource(self):
        everything = ["DOC-001", "WID-001", "ART-018", "WID-002", "FLOW-002", "ED-029", "RUN-001"]
        self.accept("acceptance-all-rows", everything)
        self.accept("acceptance-gate-two", ["GATE-002"], milestone="M1")
        self.accept("acceptance-gate-three", ["GATE-003"], milestone="M2")
        finals = [f"FINAL-00{i}" for i in range(1, 8)]
        self.accept("acceptance-final", finals)
        named = everything + ["GATE-002", "GATE-003"] + finals
        with self.assertRaisesRegex(VALIDATOR.AuditError, "every migration resource accepted"):
            self.audit(named)
        self.accept("acceptance-resources", resources=[self.resource_id(0), self.resource_id(1)])
        report = self.audit(named, [self.resource_id(0), self.resource_id(1)])
        self.assertTrue(report["product_acceptance"])
        self.assertEqual(report["accepted_migration_entries"], 2)

    # Failures the roadmap names ----------------------------------------------
    def test_rejects_partial_conjuncts_and_constraints(self):
        for mutate, error in (
                (lambda proof: proof["acceptance_evidence_needed"].pop(), "leaves conjuncts unproven"),
                (lambda proof: proof["acceptance_evidence_needed"][1].update(result="failed"), "Unproven or failed"),
                (lambda proof: proof["acceptance_evidence_needed"][0].update(evidence=["not-recorded.log"]), "cites no recorded evidence"),
                (lambda proof: proof["constraints"].clear(), "leaves constraints unproven"),
                (lambda proof: proof.update(requirement="WID-001 works partly."), "different requirement text")):
            with self.subTest(error=error):
                self.setUp_fresh()
                record = self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
                mutate(record["requirements"]["WID-001"])
                self.publish(record)
                with self.assertRaisesRegex(VALIDATOR.AuditError, error):
                    self.audit(["DOC-001", "WID-001"])

    def test_rejects_stale_evidence_and_changed_sources(self):
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        self.write(WIDGET, "int Widget() { return 2; }\n")
        with self.assertRaisesRegex(VALIDATOR.AuditError, "changed after engine revision"):
            self.audit(["DOC-001", "WID-001"])

    def test_rejects_hashes_that_do_not_describe_the_engine_revision(self):
        self.write(WIDGET, "int Widget() { return 3; }\n")
        self.commit_register(self.current, "Widget changes after the evidence")
        self.current["baseline"]["engine_revision"] = self.baseline
        self.previous = copy.deepcopy(self.current)
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])  # still claims the first revision
        with self.assertRaisesRegex(VALIDATOR.AuditError, "does not describe engine revision"):
            self.audit(["DOC-001", "WID-001"])

    def test_rejects_engine_revision_outside_the_audited_history(self):
        self.git("checkout", "--quiet", "-b", "side")
        self.write("docs/dev/ui/side.md", "# Side\n")
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Side branch")
        side = self.head()
        self.git("checkout", "--quiet", "-")
        record = self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        record["engine_revision"] = side
        self.publish(record)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "not in the audited history"):
            self.audit(["DOC-001", "WID-001"])

    def test_rejects_missing_dependencies(self):
        self.accept("acceptance-widget", ["WID-001"])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Missing dependency acceptance: WID-001 needs DOC-001"):
            self.audit(["WID-001"])

    def test_rejects_dependency_proofs_that_skip_a_supersession(self):
        self.accept("acceptance-art", ["ART-018"])
        record = self.accept("acceptance-checkbox", ["WID-002"])
        record["requirements"]["WID-002"]["dependencies"] = {"ART-003": {"verified_by": ["acceptance-art"]}}
        self.publish(record)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "cover dependencies exactly"):
            self.audit(["ART-018", "WID-002"])

    def test_gate_rejects_other_milestones_and_pending_dependencies(self):
        record = self.accept("acceptance-settings-exit", ["GATE-003"], milestone="M2")
        record["requirements"]["GATE-003"]["dependencies"]["ED-029"]["milestone"] = "M3"
        self.publish(record)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "not at the accepted gate's milestone"):
            self.audit(["GATE-003"])
        self.setUp_fresh()
        self.accept("acceptance-integration-exit", ["GATE-002"], milestone="M1")
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Missing dependency acceptance: GATE-002 needs RUN-001"):
            self.audit(["GATE-002"])

    def test_final_gate_rejects_unverified_requirements(self):
        self.accept("acceptance-settings-exit", ["GATE-003"], milestone="M2")
        self.accept("acceptance-final", ["FINAL-001"])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "every applicable requirement verified"):
            self.audit(["GATE-003", "FINAL-001"])

    def test_rejects_unqualified_resources(self):
        for mutate, error in (
                (lambda record, entry: record["resources"][self.resource_id(0)].update(artwork=[]), "lacks artwork proof"),
                (lambda record, entry: record["resources"][self.resource_id(0)].update(behavior=[]), "lacks behavior proof"),
                (lambda record, entry: record["resources"][self.resource_id(0)]["artwork"][0].update(evidence=["operation:0"]),
                 "cites no rendered image"),
                (lambda record, entry: entry.update(family_reviewed=False), "is not reviewed and accepted"),
                (lambda record, entry: (entry.update(bitmap_exceptions=[{"path": "gfx/a.tga", "reason": "", "review": ""}]),
                                        record["resources"][self.resource_id(0)].update(bitmap_exceptions=entry["bitmap_exceptions"])),
                 "unreviewed bitmap exceptions"),
                (lambda record, entry: record["source_or_asset_hashes"].pop(MENU), "replacement is not bound")):
            with self.subTest(error=error):
                self.setUp_fresh()
                record = self.accept("acceptance-menu", resources=[self.resource_id(0)])
                mutate(record, self.current_manifest["resources"][0])
                self.publish(record)
                with self.assertRaisesRegex(VALIDATOR.AuditError, error):
                    self.audit(resources=[self.resource_id(0)])

    def test_rejects_broad_or_implicit_claims(self):
        record = self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Only the named requirements may change: WID-001"):
            self.audit(["DOC-001"])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Name each requirement or resource"):
            self.audit()
        self.setUp_fresh()
        record = self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        self.rows()["WID-001"].update(copy.deepcopy(self.rows(self.previous)["WID-001"]))
        with self.assertRaisesRegex(VALIDATOR.AuditError, "claims beyond what this transition accepts"):
            self.audit(["DOC-001"])
        self.setUp_fresh()
        self.accept("acceptance-menu", resources=[self.resource_id(0)])
        self.current_manifest["resources"][1]["family_reviewed"] = True
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Only the named resources may change"):
            self.audit(resources=[self.resource_id(0)])

    def test_rejects_records_that_are_not_separately_reviewed_or_complete(self):
        for mutate, error in (
                (lambda record: record["reviewer_or_review_outcome"].update(reviewer="ui implementer"), "review separate from its author"),
                (lambda record: record["reviewer_or_review_outcome"].update(outcome="needs work"), "not accepted in review"),
                (lambda record: record.pop("limitations"), "schema differs"),
                (lambda record: record.update(result="partial"), "did not pass"),
                (lambda record: record["source_or_asset_hashes"].pop(VISUAL), "does not bind normative source"),
                (lambda record: record["log_and_image_hashes_if_rendered"].update({".tmp/run/menu.png": "0" * 64}), "Artifact hash mismatch"),
                (lambda record: record.update(rendered=False), "present exactly when rendered")):
            with self.subTest(error=error):
                self.setUp_fresh()
                record = self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
                mutate(record)
                self.publish(record)
                with self.assertRaisesRegex(VALIDATOR.AuditError, error):
                    self.audit(["DOC-001", "WID-001"])

    def test_rejects_uncommitted_and_modified_acceptance_records(self):
        record = self.accept("acceptance-document", ["DOC-001"], commit=False)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "must be committed with its current bytes"):
            self.audit(["DOC-001"])
        self.publish(record)
        self.assertEqual(self.audit(["DOC-001"])["accepted_requirements"], ["DOC-001"])
        record["limitations"] = "Review edited after the record was committed."
        self.publish(record, commit=False)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "must be committed with its current bytes"):
            self.audit(["DOC-001"])

    def test_artwork_rejects_logs_and_nonimage_files(self):
        resource = self.resource_id(0)
        record = self.accept("acceptance-menu", resources=[resource])
        for evidence in ([".tmp/run/openq4.log"], [".tmp/run/menu.png"]):
            with self.subTest(evidence=evidence):
                if evidence == [".tmp/run/menu.png"]:
                    self.write(".tmp/run/menu.png", "a log renamed to look like a screenshot")
                    record["log_and_image_hashes_if_rendered"][".tmp/run/menu.png"] = self.hash(".tmp/run/menu.png")
                record["resources"][resource]["artwork"][0]["evidence"] = evidence
                self.publish(record)
                with self.assertRaisesRegex(VALIDATOR.AuditError, "artwork proof cites no rendered image"):
                    self.audit(resources=[resource])

    def test_artwork_accepts_engine_screenshot_formats(self):
        resource = self.resource_id(0)
        record = self.accept("acceptance-menu", resources=[resource])
        # A 1x1 uncompressed BGR engine screenshot, with an 18-byte TGA header.
        tga = self.root / ".tmp/run/menu.tga"
        tga.write_bytes(bytes((0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 32, 0, 0, 0)))
        record["log_and_image_hashes_if_rendered"][".tmp/run/menu.tga"] = self.hash(".tmp/run/menu.tga")
        record["resources"][resource]["artwork"][0]["evidence"] = [".tmp/run/menu.tga"]
        self.publish(record)
        self.assertEqual(self.audit(resources=[resource])["accepted_resources"], [resource])

    def test_rejects_control_characters_in_git_evidence_paths(self):
        record = self.accept("acceptance-document", ["DOC-001"])
        record["source_or_asset_hashes"]["src/ui/Widget.cpp\nHEAD:src/ui/Widget.cpp"] = self.hash(WIDGET)
        self.publish(record)
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Invalid acceptance evidence path"):
            self.audit(["DOC-001"])

    def test_checkpoint_rechecks_earlier_binary_log_and_image_artifacts(self):
        self.accept("acceptance-document", ["DOC-001"])
        self.accept("acceptance-menu", resources=[self.resource_id(0)])
        self.audit(["DOC-001"], [self.resource_id(0)])
        self.land()
        for name in (".tmp/bin/openQ4-client_x64.exe", ".tmp/run/openq4.log", ".tmp/run/menu.png"):
            path = self.root / name
            original = path.read_bytes()
            for missing in (False, True):
                with self.subTest(artifact=name, missing=missing):
                    if missing:
                        path.unlink()
                    else:
                        path.write_bytes(b"changed artifact")
                    try:
                        with self.assertRaisesRegex(VALIDATOR.AuditError, "Accepted scope has stale evidence"):
                            self.increment()
                    finally:
                        path.write_bytes(original)
        self.assertTrue(self.increment()["earlier_acceptance_current"])

    # Earlier acceptance and the other modes ---------------------------------
    def test_checkpoint_modes_never_accept(self):
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Unauthorized acceptance/status change"):
            self.increment()
        self.save_register(self.current)
        args = argparse.Namespace(root=self.root, baseline=self.baseline, register=None, partial=[], defer_hashes=False,
                                  revision=True, withdraw=[], withdraw_resource=[])
        with self.assertRaisesRegex(VALIDATOR.AuditError, "cannot add or remove evidence records|cannot edit an existing requirement"):
            VALIDATOR.validate_revision(args)

    def test_checkpoint_keeps_current_acceptance_and_withdraws_stale_acceptance(self):
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        self.accept("acceptance-menu", resources=[self.resource_id(0)])
        self.audit(["DOC-001", "WID-001"], [self.resource_id(0)])
        self.land()
        self.assertTrue(self.increment()["earlier_acceptance_current"])
        self.write(WIDGET, "int Widget() { return 4; }\n")
        with self.assertRaisesRegex(VALIDATOR.AuditError, "Accepted scope has stale evidence"):
            self.increment()
        rows = self.rows()
        for name in ("DOC-001", "WID-001"):
            rows[name].update(status="partial", acceptance_evidence=[], remaining="Accept again after the change.")
            rows[name]["current_evidence"] = rows[name]["current_evidence"] + ["acceptance-widgets"]
        entry = self.current_manifest["resources"][0]
        entry.update(translation_status="pending", replacement=None, bitmap_exceptions=None, behavior_evidence=[], visual_evidence=[])
        self.current["baseline"]["translated_or_accepted_resources"] = 0
        report = self.increment(withdraw=["DOC-001", "WID-001"], withdraw_resource=[self.resource_id(0)])
        self.assertEqual(report["withdrawn_requirements"], ["DOC-001", "WID-001"])
        self.assertEqual(report["accepted_migration_entries"], 0)
        rows["WID-001"]["current_evidence"] = ["foundation"]
        with self.assertRaisesRegex(VALIDATOR.AuditError, "keep the acceptance records as evidence history"):
            self.increment(withdraw=["DOC-001", "WID-001"], withdraw_resource=[self.resource_id(0)])

    def test_command_line_requires_the_acceptance_mode(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/ui/validate_product_requirements.py"), "--root", str(self.root),
                                 "--baseline", self.baseline, "--accept", "DOC-001", "--output", str(self.root / ".tmp/report.json")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("--accept and --accept-resource need --acceptance", result.stderr)
        self.accept("acceptance-widgets", ["DOC-001", "WID-001"])
        self.save_register(self.current)
        result = subprocess.run([sys.executable, str(ROOT / "tools/ui/validate_product_requirements.py"), "--root", str(self.root),
                                 "--baseline", self.baseline, "--acceptance", "--accept", "DOC-001", "--accept", "WID-001",
                                 "--output", str(self.root / ".tmp/report.json")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("acceptance_transition_passed", result.stdout)

    def setUp_fresh(self):
        """Start a subtest from a clean fixture."""
        self.clean_fixture(self.root)
        self.setUp()


if __name__ == "__main__":
    unittest.main(verbosity=2)
