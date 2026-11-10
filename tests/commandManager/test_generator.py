"""Focused unit tests for safe declaration loading and stable generation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import contextlib
import io

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "commandManager/generator"))
from definition_loader import load_definition, DefinitionError
from rm_command_gen import main, write_changed
from generation_output import plan_output, source_list

VALID = '''rm_module(name="demo", summary="示例")
rm_define(name="run", summary="运行", usage="demo.run --count <1..3>",
params=[{"name":"count", "binding":"option", "option":"--count", "type":"int", "ranges":[[1,3]], "default":2}])
'''


class OutputPlanningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "generated"

    def test_planning_does_not_create_or_modify_files(self):
        plan = plan_output(self.root, {"demo/Demo.cpp": "// hello\r\n"})
        self.assertFalse(self.root.exists())
        self.assertEqual(plan.deletes, ())
        self.assertEqual(plan.writes[0][1], b"\xef\xbb\xbf// hello\n")

    def test_malformed_ownership_is_a_definition_error(self):
        self.root.mkdir()
        manifest = self.root / ".rm-generated.json"
        for records in [[], {"demo.cpp": 1}, {"demo.cpp": "invalid"}, {"../outside": "0" * 64}]:
            with self.subTest(records=records):
                manifest.write_text(json.dumps(records), encoding="utf-8")
                with self.assertRaises(DefinitionError):
                    plan_output(self.root, {"demo.cpp": "generated"})
        self.assertFalse((self.root / "demo.cpp").exists())

    def test_validation_does_not_publish_earlier_valid_outputs(self):
        self.root.mkdir()
        unmanaged = self.root / "user.cpp"
        unmanaged.write_text("user code", encoding="utf-8")
        with self.assertRaises(DefinitionError):
            plan_output(self.root, {"safe.cpp": "generated", "user.cpp": "replacement"})
        self.assertFalse((self.root / "safe.cpp").exists())
        self.assertEqual(unmanaged.read_text(encoding="utf-8"), "user code")

    def test_check_returns_no_writes_or_deletes_and_preserves_timestamps(self):
        for path, data in plan_output(self.root, {"demo.md": "hello"}).writes:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        before = {p: p.stat().st_mtime_ns for p in self.root.iterdir()}
        plan = plan_output(self.root, {"demo.md": "hello"}, check=True)
        self.assertEqual(plan.writes, ())
        self.assertEqual(plan.deletes, ())
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in self.root.iterdir()})

    def test_source_list_retains_all_modules_in_stable_order(self):
        cmake = source_list({"one/One.cpp": "", "two/Two.h": ""}, {"one.md": ""}).decode()
        self.assertIn('RM_GENERATED_SOURCES "${RM_COMMAND_OUTPUT}/one/One.cpp"', cmake)
        self.assertIn('RM_GENERATED_HEADERS "${RM_COMMAND_OUTPUT}/two/Two.h"', cmake)
        self.assertIn('RM_GENERATED_DOCS "${RM_COMMAND_DOCS}/one.md"', cmake)

    def test_write_failure_cleans_temporary_file_and_preserves_target(self):
        self.root.mkdir()
        target = self.root / "generated.cpp"
        target.write_bytes(b"previous generation")
        staging = self.root / "staging"
        original_factory = tempfile.NamedTemporaryFile

        class FailedStream:
            def __init__(self, name):
                self.name = name

            def write(self, data):
                raise OSError("simulated disk full")

        @contextlib.contextmanager
        def failed_writer(**kwargs):
            with original_factory(**kwargs) as stream:
                yield FailedStream(stream.name)

        with patch("rm_command_gen.tempfile.NamedTemporaryFile", failed_writer):
            with self.assertRaisesRegex(OSError, "simulated disk full"):
                write_changed(target, b"next generation", staging)
        self.assertEqual(target.read_bytes(), b"previous generation")
        self.assertEqual(list(staging.iterdir()), [])


class DefinitionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.file = self.root / "demo.rmdef"

    def tearDown(self):
        self.temp.cleanup()

    def load(self, text):
        self.file.write_text(text, encoding="utf-8")
        return load_definition(self.file)

    def test_literals_and_unicode(self):
        self.assertEqual(self.load(VALID)["commands"][0]["params"][0]["default"], 2)

    def test_reject_executable_syntax(self):
        for text in ["import os", "rm_module(**{})", "x=1", "rm_module(name=str(3))", "[rm_define() for x in []]"]:
            with self.subTest(text=text), self.assertRaises(DefinitionError):
                self.load(text)

    def test_reject_ambiguous_or_invalid_definitions(self):
        invalid = [VALID.replace('usage="demo.run --count <1..3>"', 'usage=" "'),
                   VALID.replace('"default":2', '"default":True'),
                   VALID.replace('"--count"', '"--help"'),
                   VALID.replace('"type":"int"', '"type":"int", "type":"float"'),
                   VALID.replace('[[1,3]]', '[[3,1]]'),
                   VALID.replace('"default":2', '"default":4'),
                   VALID.replace('summary="运行"', 'summary="运行", handler="business"')]
        for text in invalid:
            with self.subTest(text=text), self.assertRaises(DefinitionError):
                self.load(text)

    def test_generated_names_and_reserved_identifiers(self):
        collision = VALID.replace('name="run"', 'name="foo_bar"') + VALID.split('\n', 1)[1].replace('name="run"', 'name="fooBar"')
        with self.assertRaises(DefinitionError):
            self.load(collision)
        for name in ("unsigned", "constexpr", "co_return", "a__b"):
            with self.subTest(name=name), self.assertRaises(DefinitionError):
                self.load(VALID.replace('"name":"count"', f'"name":"{name}"'))

    def test_check_and_docs_are_independent_of_sources(self):
        self.file.write_text(VALID, encoding="utf-8")
        self.assertEqual(main(["check", "--input", str(self.file)]), 0)
        manifest = self.root / "modules.json"
        manifest.write_text(json.dumps({"schema": 1, "modules": [{"name": "demo", "file": "demo.rmdef"}]}))
        docs = self.root / "docs"
        args = ["docs", "--manifest", str(manifest), "--docs-output", str(docs), "--work-dir", str(self.root / "build/generator")]
        self.assertEqual(main(args), 0)
        metadata = json.loads((docs / "commands.help.json").read_text("utf-8"))
        self.assertEqual(metadata["modules"][0]["commands"][0]["usage"], "demo.run --count <1..3>")
        self.assertEqual(list(self.root.rglob("*.cpp")), [])
        missing = self.root / "missing-templates"
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(args + ["--templates", str(missing)]), 2)
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(main([]), 0)
        self.assertIn("--docs-output", output.getvalue())

    def test_stable_generation_partial_module_and_stale_cleanup(self):
        definitions = self.root / "product/commandDefinitions"
        definitions.mkdir(parents=True)
        (definitions / "demo.rmdef").write_text(VALID, encoding="utf-8")
        (definitions / "other.rmdef").write_text(VALID.replace('"demo"', '"other"'), encoding="utf-8")
        manifest = definitions / "modules.json"
        entries = [{"name": name, "file": name + ".rmdef"} for name in ("demo", "other")]
        manifest.write_text(json.dumps({"schema": 1, "modules": entries}))
        out = self.root / "generated"
        args = ["--manifest", str(manifest), "--output", str(out), "--docs", str(self.root / "docs"), "--build-dir", str(self.root / "build/config")]
        self.assertEqual(main(args), 0)
        target = out / "demo/RunCommand.cpp"
        stamp = target.stat().st_mtime_ns
        self.assertEqual(main(args), 0)
        self.assertEqual(stamp, target.stat().st_mtime_ns)
        self.assertTrue(target.read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertEqual(main(args + ["--module", "demo"]), 0)
        self.assertIn("other::registerOtherModule", (out / "CommandModules.cpp").read_text("utf-8-sig"))
        metadata = json.loads((self.root / "docs/commands.help.json").read_text("utf-8"))
        self.assertEqual([m["name"] for m in metadata["modules"]], ["demo", "other"])
        self.assertEqual(main(args + ["--check"]), 0)
        handwritten = out / "manual.cpp"
        handwritten.write_text("must survive")
        manifest.write_text(json.dumps({"schema": 1, "modules": entries[:1]}))
        self.assertEqual(main(args), 0)
        self.assertFalse((out / "other/RunCommand.cpp").exists())
        self.assertTrue(handwritten.exists())


if __name__ == "__main__":
    unittest.main()
