"""envBuild 的行为单元测试：帮助、配置、边界保护及失败编排。"""

from argparse import Namespace
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch

import envBuild
from pythonProject.build import check_cache, clean, compile_project, remove_tree
from pythonProject.deploy import clean_runtime, digest, prepare, verify_runtime
from pythonProject.environment import BuildError, Environment, checked_path, initialize
from pythonProject.package import package_project


class CommandHelpTests(unittest.TestCase):
    def invoke(self, args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err), patch.object(Environment, "read") as read:
            try:
                code = envBuild.main(args)
            except SystemExit as error:
                code = error.code
            read.assert_not_called()
        return code, out.getvalue(), err.getvalue()

    def test_empty_command_and_global_help_do_not_load_environment(self):
        outputs = []
        for args in ([], ["-h"], ["--help"]):
            with self.subTest(args=args):
                code, output, _ = self.invoke(args)
                self.assertEqual(code, 0)
                self.assertIn("prepareRun", output)
                self.assertNotIn("deployqt", output)
                self.assertIn("docs/envBuild命令使用指南.md", output)
                outputs.append(output)
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[0], outputs[2])

    def test_overview_lists_full_parameters_at_narrow_and_wide_widths(self):
        for width in (76, 220):
            with self.subTest(width=width), patch(
                    "pythonProject.cli_help.shutil.get_terminal_size",
                    return_value=os.terminal_size((width, 30))):
                code, output, _ = self.invoke([])
                self.assertEqual(code, 0)
                compact = " ".join(output.split())
                self.assertIn("python envBuild.py info [-h | --help]", compact)
                self.assertNotIn("info [--dry-run]", compact)
                self.assertIn(
                    "python envBuild.py compile [release | debug | release_debug] "
                    "[--target {ReverseModeler | ReverseServiceTests}] [--jobs <数量>] "
                    "[--rebuild] [--fresh] [--dry-run] [-h | --help]", compact)
                self.assertIn(
                    "python envBuild.py clean [out | 3rd | out_3rd] "
                    "[release | debug | release_debug] [--dry-run] [-h | --help]", compact)
                self.assertIn(
                    "python envBuild.py clean {build | package | all} "
                    "[--dry-run] [-h | --help]", compact)
                for command in ("prepareRun", "package"):
                    self.assertIn(f"python envBuild.py {command} "
                                  "[release | debug | release_debug] "
                                  "[--dry-run] [-h | --help]", compact)
                self.assertNotIn("\x1b[", output)

    def test_help_examples_are_accepted_without_executing_commands(self):
        parser = envBuild.make_parser()
        with patch.object(Environment, "read") as read:
            for examples in envBuild.EXAMPLES.values():
                for example in examples:
                    with self.subTest(example=example):
                        options = parser.parse_args(shlex.split(example))
                        self.assertIn(options.command, envBuild.DESCRIPTIONS)
                        if options.command == "clean" and options.kind in ("build", "package", "all"):
                            self.assertIsNone(options.mode)
            read.assert_not_called()

    def test_every_subcommand_has_two_help_forms(self):
        for command in envBuild.DESCRIPTIONS:
            for flag in ("-h", "--help"):
                with self.subTest(command=command, flag=flag):
                    code, output, _ = self.invoke([command, flag])
                    self.assertEqual(code, 0)
                    self.assertIn(envBuild.DESCRIPTIONS[command], output)
                    self.assertIn("示例", output)

    def test_command_typo_and_removed_command_do_not_execute(self):
        for name, hint in (("complie", "compile"), ("deployqt", "prepareRun")):
            code, _, error = self.invoke([name])
            self.assertEqual(code, 2)
            self.assertIn(hint, error)

    def test_invalid_arguments_show_relevant_help(self):
        for args in (["compile", "releas"], ["compile", "--target"],
                     ["compile", "--rebuid"], ["compile", "--reb"],
                     ["compile", "--jobs", "0"], ["compile", "--jobs", "a"]):
            with self.subTest(args=args):
                code, _, error = self.invoke(args)
                self.assertEqual(code, 2)
                self.assertIn("--target", error)
                self.assertNotIn("Traceback", error)

    def test_shared_clean_cannot_select_single_configuration(self):
        for kind in ("build", "package", "all"):
            with self.subTest(kind=kind):
                code, _, error = self.invoke(["clean", kind, "debug"])
                self.assertEqual(code, 2)
                self.assertIn("共享目录", error)
                self.assertIn("清理范围：", error)
                self.assertIn("已完成 ZIP", error)


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="envbuild-中文 空格-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.presets = {"version": 6, "configurePresets": [
            {"name": "base", "hidden": True, "generator": "Visual Studio 17 2022",
             "architecture": "x64", "toolset": "v143",
             "binaryDir": "${sourceDir}/build/${presetName}"},
            {"name": "vc143-cpp20", "inherits": "base"}]}
        self.write("CMakePresets.json", json.dumps(self.presets))
        self.write("envBuild.example.ini", "[build]\nconfigure_preset=vc143-cpp20\n"
                   "default_mode=release\nparallel_jobs=0\n[dependencies]\nprefix_paths= SDK 中文\n"
                   "[tools]\n[runtime]\n")
        self.env = Environment.read(self.root)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def make_product(self, mode="release"):
        exe = self.write(f"output/vc143-cpp20/{mode}/ReverseModeler.exe", "executable")
        script = self.write(f"build/vc143-cpp20/envbuild/{mode.title()}/deploy.cmake", "# script")
        data = {"source": self.root.as_posix(), "configuration": mode.title(),
                "executable": exe.as_posix(), "deploy_script": script.as_posix(),
                "tag": "vc143-cpp20", "version": "0.1.0", "commit": "abc123", "dirty": True}
        self.write(f"build/vc143-cpp20/envbuild/{mode.title()}/build-info.json", json.dumps(data))
        self.write("build/vc143-cpp20/CPackConfig.cmake", "# package")
        return exe

    def make_manifest(self, exe, paths):
        data = {"executable_sha256": digest(exe), "files": [
            {"path": name, "sha256": digest(exe.parent / name)} for name in paths]}
        manifest = exe.parent / ".runtime-ReverseModeler.json"
        manifest.write_text(json.dumps(data), encoding="utf-8")
        return manifest

    def test_relative_sdk_is_relative_to_repository(self):
        self.assertEqual(self.env.prefixes, ((self.root / "SDK 中文").as_posix(),))
        self.assertEqual(self.env.build_dir, self.root / "build/vc143-cpp20")

    def test_init_does_not_overwrite_existing_configuration(self):
        path = self.write("envBuild.local.ini", "[build]\ndefault_mode=debug\n")
        before = path.read_bytes()
        with redirect_stdout(io.StringIO()):
            initialize(self.root)
        self.assertEqual(path.read_bytes(), before)

    def test_init_dry_run_does_not_write(self):
        with redirect_stdout(io.StringIO()):
            initialize(self.root, True)
        self.assertFalse((self.root / "envBuild.local.ini").exists())

    def test_sdk_cache_mismatch_requires_explicit_fresh(self):
        self.write("build/vc143-cpp20/CMakeCache.txt",
                   f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root.as_posix()}\n"
                   "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
                   "CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n"
                   "CMAKE_GENERATOR_TOOLSET:INTERNAL=v143\n"
                   "CMAKE_PREFIX_PATH:PATH=X:/different\n")
        with self.assertRaisesRegex(BuildError, "SDK"):
            check_cache(self.env, False)
        check_cache(self.env, True)

    def test_preset_cannot_escape_build_directory(self):
        self.presets["configurePresets"][0]["binaryDir"] = "${sourceDir}/../outside"
        self.write("CMakePresets.json", json.dumps(self.presets))
        with self.assertRaises(BuildError):
            Environment.read(self.root)

    def test_boundary_and_parent_traversal_are_rejected(self):
        boundary = self.root / "output"
        for path in (boundary, boundary / "../data.pcd", self.root / "external"):
            with self.subTest(path=path), self.assertRaises(BuildError):
                checked_path(path, boundary)

    def test_cleanup_preserves_unmanaged_and_modified_files(self):
        exe = self.make_product()
        self.write("output/vc143-cpp20/release/library.dll", "original")
        changed = self.write("output/vc143-cpp20/release/changed.dll", "original")
        user = self.write("output/vc143-cpp20/release/model.pcd", "user data")
        self.make_manifest(exe, ["library.dll", "changed.dll"])
        changed.write_text("user modification", encoding="utf-8")
        with redirect_stdout(io.StringIO()):
            clean_runtime(self.env, "release")
        self.assertTrue(exe.exists())
        self.assertTrue(changed.exists())
        self.assertTrue(user.exists())
        self.assertFalse((exe.parent / "library.dll").exists())

    def test_malicious_manifest_does_not_delete_external_file(self):
        exe = self.make_product()
        outside = self.write("important.txt", "preserve")
        manifest = exe.parent / ".runtime-ReverseModeler.json"
        manifest.write_text(json.dumps({"files": [{"path": outside.as_posix(),
                                                    "sha256": digest(outside)}]}), encoding="utf-8")
        with self.assertRaises(BuildError):
            clean_runtime(self.env, "release")
        self.assertTrue(outside.exists())

    def test_clean_dry_run_preserves_files(self):
        exe = self.make_product()
        library = self.write("output/vc143-cpp20/release/library.dll", "lib")
        self.make_manifest(exe, ["library.dll"])
        with redirect_stdout(io.StringIO()):
            clean_runtime(self.env, "release", True)
        self.assertTrue(library.exists())

    def test_missing_runtime_is_detected_before_launch(self):
        exe = self.make_product()
        library = self.write("output/vc143-cpp20/release/library.dll", "lib")
        self.make_manifest(exe, ["library.dll"])
        verify_runtime(exe)
        library.unlink()
        with self.assertRaisesRegex(BuildError, "prepareRun"):
            verify_runtime(exe)

    def test_prepare_calls_only_generated_script(self):
        self.make_product()
        with patch.object(Environment, "tool", return_value=Path("cmake.exe")), \
                patch.object(Environment, "run") as run:
            prepare(self.env, "release")
        args = run.call_args.args[0]
        self.assertIn("-P", args)
        self.assertNotIn("--build", args)
        self.assertIn("-DRM_FORCE=TRUE", args)

    def test_prepare_missing_executable_does_not_compile(self):
        self.make_product().unlink()
        with patch.object(Environment, "run") as run, self.assertRaises(BuildError):
            prepare(self.env, "release")
        run.assert_not_called()

    def test_clean_package_keeps_completed_archives(self):
        archive = self.write("output/packages/published.zip", "published")
        stage = self.write("output/package-staging/attempt/file", "temp")
        with redirect_stdout(io.StringIO()):
            clean(self.env, "package", None)
        self.assertTrue(archive.exists())
        self.assertFalse(stage.exists())

    def test_compile_passes_chinese_paths_as_individual_arguments(self):
        options = Namespace(mode="release", jobs=2, fresh=False, rebuild=False,
                            target="ReverseModeler", dry_run=True)
        with patch.object(Environment, "tool", return_value=Path("cmake.exe")), \
                patch.object(Environment, "run") as run:
            compile_project(self.env, options)
        configure = run.call_args_list[0].args[0]
        self.assertIn("-DCMAKE_PREFIX_PATH=" + self.env.prefixes[0], configure)
        self.assertEqual(run.call_count, 3)

    def test_compile_failure_stops_before_deploy(self):
        options = Namespace(mode="release", jobs=None, fresh=False, rebuild=False,
                            target="ReverseModeler", dry_run=False)
        with patch.object(Environment, "tool", return_value=Path("cmake.exe")), \
                patch.object(Environment, "run", side_effect=[None, BuildError("build failed")]) as run:
            with self.assertRaises(BuildError):
                compile_project(self.env, options)
        self.assertEqual(run.call_count, 2)

    def test_cpack_failure_does_not_publish_partial_package(self):
        self.make_product()
        options = Namespace(command="package", mode="release", dry_run=False)
        with patch.object(Environment, "tool", return_value=Path("cpack.exe")), \
                patch.object(Environment, "run", side_effect=BuildError("cpack failed")):
            with self.assertRaises(BuildError):
                package_project(self.env, options)
        self.assertFalse((self.root / "output/packages").exists())

    def test_package_does_not_run_compile(self):
        self.make_product()
        options = Namespace(command="package", mode="release", dry_run=True)
        with patch.object(Environment, "tool", return_value=Path("cpack.exe")), \
                patch.object(Environment, "run") as run, \
                patch("pythonProject.package.compile_project") as compile_mock:
            package_project(self.env, options)
        compile_mock.assert_not_called()
        self.assertIn("-C", run.call_args.args[0])
        self.assertIn("Release", run.call_args.args[0])

    def test_package_publishes_short_staging_name_with_matching_checksum(self):
        self.make_product()
        options = Namespace(command="package", mode="release", dry_run=False)

        def cpack(command, dry_run):
            work = Path(command[command.index("-B") + 1])
            work.mkdir(parents=True)
            archive = work / "ReverseModeler.zip"
            archive.write_bytes(b"packaged bytes")
            archive.with_suffix(".zip.sha256").write_text(
                f"{digest(archive)}  ReverseModeler.zip\n", encoding="utf-8")

        with patch.object(Environment, "tool", return_value=Path("cpack.exe")), \
                patch.object(Environment, "run", side_effect=cpack):
            package_project(self.env, options)
        archives = list((self.root / "output/packages").glob("*.zip"))
        self.assertEqual(len(archives), 1)
        archive = archives[0]
        self.assertIn("release-abc123-dirty", archive.name)
        self.assertEqual(archive.with_suffix(".zip.sha256").read_text(encoding="utf-8"),
                         f"{digest(archive)}  {archive.name}\n")

    def test_package_invalid_checksum_does_not_publish(self):
        self.make_product()
        options = Namespace(command="package", mode="release", dry_run=False)

        def cpack(command, dry_run):
            work = Path(command[command.index("-B") + 1])
            work.mkdir(parents=True)
            (work / "ReverseModeler.zip").write_bytes(b"partial package")
            (work / "ReverseModeler.zip.sha256").write_text("invalid  ReverseModeler.zip\n")

        with patch.object(Environment, "tool", return_value=Path("cpack.exe")), \
                patch.object(Environment, "run", side_effect=cpack), self.assertRaises(BuildError):
            package_project(self.env, options)
        self.assertFalse(list((self.root / "output/packages").glob("*.zip")))


if __name__ == "__main__":
    unittest.main()
