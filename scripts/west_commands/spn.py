import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand

sys.path.insert(0, os.path.dirname(__file__))
from spn_common import (
    Logger,
    Config,
    PathUtils,
    TwisterOpts,
    SubprocessRunner,
    env_with,
    find_source_files,
    find_app_roots,
)
from external_tools import CLANG_FORMAT, RUN_CLANG_TIDY, GCOVR


class spn(WestCommand):
    def __init__(self):
        super().__init__(
            name="spn",
            help="SPN development sugar",
            description="Hassle-free building, testing, and configuring SPN components",
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name, help=self.help, description=self.description
        )

        subparsers = parser.add_subparsers(dest="command", required=True)

        menuconfig = subparsers.add_parser(
            "menuconfig", help="Run menuconfig for sample/test"
        )
        menuconfig.add_argument("path", help="path to sample/test")
        menuconfig.add_argument(
            "--board", "-b", default=Config.default_board(), help="target board"
        )
        menuconfig.set_defaults(func=self.do_menuconfig)

        build = subparsers.add_parser("build", help="build sample/test or all")
        build.add_argument(
            "path", nargs="?", help="path to sample/test (omit to build all)"
        )
        build.add_argument(
            "--board", "-b", default=Config.default_board(), help="target board"
        )
        build.add_argument(
            "--jobs", "-j", type=int, help="parallel jobs (default: nproc/2)"
        )
        build.add_argument(
            "--verbose", "-v", action="store_true", help="verbose output"
        )
        build.add_argument(
            "--output",
            "-O",
            default=Config.build_samples_dir(),
            help="twister output directory",
        )
        build.add_argument(
            "--asan", action="store_true", help="build with ASAN/UBSAN sanitizers"
        )
        build.set_defaults(func=self.do_build)

        test = subparsers.add_parser("test", help="run test or all tests")
        test.add_argument(
            "path", nargs="?", help="path to test (omit to run all tests)"
        )
        test.add_argument(
            "--board", "-b", default=Config.default_board(), help="target board"
        )
        test.add_argument(
            "--jobs", "-j", type=int, help="parallel jobs (default: nproc/2)"
        )
        test.add_argument("--verbose", "-v", action="store_true", help="verbose output")
        test.add_argument(
            "--output",
            "-O",
            default=Config.build_tests_dir(),
            help="twister output directory",
        )
        test.add_argument(
            "--asan", action="store_true", help="run with ASAN/UBSAN sanitizers"
        )
        test.set_defaults(func=self.do_test)

        clean = subparsers.add_parser("clean", help="remove build artifacts")
        clean.set_defaults(func=self.do_clean)

        format_cmd = subparsers.add_parser("format", help="format C++ source files")
        format_cmd.add_argument(
            "--check", action="store_true", help="only check formatting"
        )
        format_cmd.set_defaults(func=self.do_format)

        tidy = subparsers.add_parser("tidy", help="run clang-tidy static analysis")
        tidy.add_argument(
            "--jobs", "-j", type=int, help="parallel jobs (default: nproc/2)"
        )
        tidy.set_defaults(func=self.do_tidy)

        cppcheck = subparsers.add_parser(
            "cppcheck", help="run cppcheck static analysis"
        )
        cppcheck.add_argument(
            "--paths", nargs="+", help="directories to analyze (default: src)"
        )
        cppcheck.add_argument(
            "--strict",
            action="store_true",
            help="enable strict mode (inconclusive, exhaustive)",
        )
        cppcheck.add_argument(
            "--verbose", "-v", action="store_true", help="verbose output"
        )
        cppcheck.add_argument(
            "--fail-on-violations",
            action="store_true",
            help="exit with error if violations found",
        )
        cppcheck.set_defaults(func=self.do_cppcheck)

        coverage = subparsers.add_parser("coverage", help="generate coverage report")
        coverage.add_argument(
            "path", nargs="?", help="path to test (omit for all tests)"
        )
        coverage.add_argument(
            "--jobs", "-j", type=int, help="parallel jobs (default: nproc/2)"
        )
        coverage.add_argument(
            "--fail-under-line",
            type=int,
            default=Config.coverage_line_threshold(),
            help=f"minimum line coverage %% (default: {Config.coverage_line_threshold()})",
        )
        coverage.add_argument(
            "--fail-under-branch",
            type=int,
            default=Config.coverage_branch_threshold(),
            help=f"minimum branch coverage %% (default: {Config.coverage_branch_threshold()})",
        )
        coverage.add_argument(
            "--fail-under-function",
            type=int,
            default=Config.coverage_function_threshold(),
            help=f"minimum function coverage %% (default: {Config.coverage_function_threshold()})",
        )
        coverage.set_defaults(func=self.do_coverage)

        ci = subparsers.add_parser("ci", help="run full CI pipeline")
        ci.add_argument(
            "--jobs", "-j", type=int, help="parallel jobs (default: nproc/2)"
        )
        ci.add_argument("--verbose", "-v", action="store_true", help="verbose output")
        ci.set_defaults(func=self.do_ci)

        run = subparsers.add_parser("run", help="build and run sample/test")
        run.add_argument("path", help="path to sample/test")
        run.add_argument(
            "--board", "-b", default=Config.default_board(), help="target board"
        )
        run.add_argument(
            "--timeout",
            "-t",
            type=int,
            default=60,
            help="execution timeout in seconds (default: 60)",
        )
        run.add_argument(
            "--asan", action="store_true", help="run with ASAN/UBSAN sanitizers"
        )
        run.set_defaults(func=self.do_run_sample)

        return parser

    def do_run(self, args, unknown_args):
        args.func(args)

    def do_menuconfig(self, args):
        """Run menuconfig for specified path, building automatically if needed"""
        PathUtils.validate_project(args.path)
        build_dir = PathUtils.compute_build_dir(args.path, args.board)

        if not os.path.isdir(build_dir):
            Logger.info(
                f"Build directory missing. Building first: {args.path} -> {build_dir}"
            )

            runner = SubprocessRunner()
            rc = runner.run(
                ["west", "build", "-b", args.board, args.path, "-d", build_dir]
            )

            if rc != 0:
                Logger.err(f"Build failed: {args.path}")
                sys.exit(rc)

            Logger.ok(f"Build successful: {args.path}")

        Logger.info(f"Running menuconfig: {args.path}")
        rc = SubprocessRunner().run(
            ["west", "build", "-d", build_dir, "-t", "menuconfig"]
        )

        if rc != 0:
            Logger.err(f"Menuconfig failed: {args.path}")
            sys.exit(rc)

        Logger.ok(f"Menuconfig completed: {args.path}")

    def do_build(self, args):
        """Build sample/test or all samples"""
        if args.path:
            PathUtils.validate_project(args.path)

            board = args.board
            base_dir = None
            if args.asan:
                board = "native_sim"
                Logger.info(f"ASAN mode enabled - using board: {board}")
                if "tests" in Path(args.path).parts:
                    base_dir = Config.build_tests_asan_dir()
                elif "samples" in Path(args.path).parts:
                    base_dir = Config.build_samples_asan_dir()

            build_dir = PathUtils.compute_build_dir(args.path, board, base_dir)
            Logger.info(f"Building: {args.path} -> {build_dir}")

            cmd = ["west", "build", "-b", board, args.path, "-d", build_dir]
            if args.asan:
                cmd.append("--")
                cmd.extend([f"-D{cfg}" for cfg in Config.asan_configs()])

            rc = SubprocessRunner().run(cmd)
            if rc != 0:
                Logger.err(f"Build failed: {args.path}")
                sys.exit(rc)

            Logger.ok(f"Build successful: {args.path}")
            return

        Logger.info("Building all samples (parallel)...")
        sample_dirs = find_app_roots(tests=False, samples=True)
        if not sample_dirs:
            Logger.err("No sample directories found")
            sys.exit(1)

        opts = TwisterOpts.build(jobs=args.jobs, verbose=args.verbose)
        test_args = [arg for d in sample_dirs for arg in ["-T", d]]

        cmd = (
                ["west", "twister"] + test_args + ["-p", args.board, "--build-only"]
                + opts
                + ["-O", args.output]
        )

        env = env_with(
            {"CMAKE_BUILD_PARALLEL_LEVEL": str(Config.ninja_jobs_per_build())}
        )
        rc = SubprocessRunner(env=env).run(cmd)
        if rc != 0:
            Logger.err("Some builds failed")
            sys.exit(rc)

        Logger.ok("All samples built")

    def do_test(self, args):
        """Run test or all tests"""
        if args.path:
            PathUtils.validate_project(args.path)
            if "tests" not in Path(args.path).parts:
                Logger.err(f"Path must be a test: {args.path}")
                sys.exit(1)

        board = args.board
        output = args.output
        asan_configs = []

        if args.asan:
            board = "native_sim"
            output = Config.build_tests_asan_dir()
            Logger.info(f"ASAN mode enabled - using board: {board}")
            asan_configs = [f"-x={cfg}" for cfg in Config.asan_configs()]

        msg = (
            f"Running tests: {args.path}"
            if args.path
            else "Running all tests (parallel)..."
        )
        if args.asan:
            msg = (
                f"Running tests with sanitizers: {args.path}"
                if args.path
                else "Running all tests with sanitizers..."
            )
        Logger.info(msg)

        opts = TwisterOpts.build(jobs=args.jobs, verbose=args.verbose)

        if args.path:
            cmd = ["west", "twister", "-T", args.path, "-p", board] + opts
        else:
            test_dirs = find_app_roots(tests=True, samples=False)
            if not test_dirs:
                Logger.err("No test directories found")
                sys.exit(1)
            test_args = [arg for d in test_dirs for arg in ["-T", d]]
            cmd = ["west", "twister"] + test_args + ["-p", board] + opts

        if args.asan:
            cmd.extend(["--enable-asan", "--enable-ubsan"])
            cmd.extend(asan_configs)

        cmd.extend(["-O", output])

        env = env_with(
            {"CMAKE_BUILD_PARALLEL_LEVEL": str(Config.ninja_jobs_per_build())}
        )
        rc = SubprocessRunner(env=env).run(cmd)

        if rc != 0:
            if args.asan:
                Logger.err(
                    f"Sanitizer tests failed: {args.path}"
                    if args.path
                    else "Sanitizer tests failed"
                )
                for exe_path in Path(output).rglob("zephyr/zephyr.exe"):
                    test_dir = exe_path.parent.parent
                    handler_log = test_dir / "handler.log"

                    if handler_log.exists():
                        Logger.err(f"Failing test: {test_dir.name}")
                        SubprocessRunner(timeout=5, stderr=subprocess.STDOUT).run(
                            [str(exe_path)]
                        )
            else:
                Logger.err(
                    f"Tests failed: {args.path}" if args.path else "Some tests failed"
                )
            sys.exit(rc)

        if args.asan:
            Logger.ok(
                f"Sanitizer tests passed: {args.path}"
                if args.path
                else "All sanitizer tests passed"
            )
        else:
            Logger.ok(f"Tests passed: {args.path}" if args.path else "All tests passed")

    def do_clean(self, args):
        """Remove build artifacts"""
        for pattern in ["build*", "twister-out*"]:
            for path in Path(".").glob(pattern):
                if path.is_dir():
                    Logger.info(f"Removing {path}/")
                    shutil.rmtree(path, ignore_errors=True)

        for build_dir in Path("src").rglob("build"):
            if build_dir.is_dir():
                Logger.info(f"Removing {build_dir}/")
                shutil.rmtree(build_dir, ignore_errors=True)

        for pattern in ["*.dump", "*.ctu-info", "*.gcov"]:
            removed = []
            for path in Path(".").rglob(pattern):
                if len(path.parts) <= 11:
                    try:
                        path.unlink(missing_ok=True)
                        removed.append(path)
                    except Exception:
                        pass
            if removed:
                Logger.info(f"Removing {len(removed)} {pattern}")

        Logger.ok("Cleanup complete")

    def do_format(self, args):
        """Format C++ source files or check formatting"""
        files = find_source_files(tests=True, samples=True)
        if not files:
            Logger.ok("No source files found")
            return

        if not args.check:
            if SubprocessRunner(timeout=120).run(["west", "spn", "format", "--check"]) == 0:
                return
            if input("Apply formatting? [y/N]: ").strip().lower() not in ['y', 'yes']:
                sys.exit(1)

        flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
        rc = SubprocessRunner(timeout=120).run([CLANG_FORMAT] + flags + files)

        if rc != 0:
            Logger.err("Formatting issues found" if args.check else "Format failed")
            sys.exit(rc)
        Logger.ok("All files properly formatted")

    def do_tidy(self, args):
        """Run clang-tidy static analysis"""
        db_dir = Config.build_compile_db_dir()
        Logger.info("Generating compile database...")
        env = env_with({"ZEPHYR_TOOLCHAIN_VARIANT": "llvm"})
        rc = SubprocessRunner(env=env).run(
            [
                "west",
                "build",
                "-b",
                Config.default_board(),
                "src/develop_target",
                "-d",
                db_dir,
                "--",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            ]
        )
        if rc != 0:
            Logger.err("Failed to generate compile database")
            sys.exit(rc)

        Logger.info("Running static analysis...")
        jobs = args.jobs if args.jobs else os.cpu_count() // 2
        src_root = Path("src").resolve()
        src_regex = re.escape(src_root.as_posix())
        log_file = Path(db_dir) / "tidy.log"

        with open(log_file, "w") as f:
            rc = SubprocessRunner(timeout=300, stdout=f, stderr=subprocess.STDOUT).run(
                [
                    RUN_CLANG_TIDY,
                    "-p",
                    db_dir,
                    f"-j{jobs}",
                    f"-source-filter=^{src_regex}/.*",
                    f"-header-filter=^{src_regex}/.*",
                    "-extra-arg=-fmacro-backtrace-limit=1",
                    "-quiet",
                    f"^{src_regex}/.*",
                ]
            )

        if rc != 0:
            with open(log_file) as f:
                print(f.read(), end="")
            Logger.err("Static analysis failed")
            sys.exit(rc)

        with open(log_file) as f:
            for line in f:
                if "warning: " in line:
                    print(line, end="")

        Logger.ok("Static analysis complete")

    def do_cppcheck(self, args):
        """Run cppcheck static analysis"""
        Logger.info("Running cppcheck analysis...")

        cmd = [
            "cppcheck",
            "--enable=warning,style,performance,portability",
            "--error-exitcode=2",
            "--force",
            "--inline-suppr",
            "--std=c++20",
            "--platform=unix32",
            "--library=zephyr",
            "--library=posix",
            "--template=gcc",
            "--quiet",
        ]

        if args.strict:
            cmd.extend(["--inconclusive", "--check-level=exhaustive"])

        if args.paths:
            cmd.extend(args.paths)
        else:
            files = find_source_files(tests=False, samples=False)
            cmd.extend(files)

        if args.verbose:
            Logger.info(f"Command: {' '.join(cmd[:10])} ... ({len(cmd) - 10} files)")

        rc = SubprocessRunner(timeout=600).run(cmd)

        if rc != 0 and args.fail_on_violations:
            Logger.err("Cppcheck found violations")
            sys.exit(rc)
        elif rc != 0:
            Logger.wrn("Cppcheck found violations (not failing)")
        else:
            Logger.ok("Cppcheck analysis complete")

    def do_coverage(self, args):
        """Generate coverage report from tests"""
        if args.path:
            PathUtils.validate_project(args.path)
            if "tests" not in Path(args.path).parts:
                Logger.err(f"Path must be a test: {args.path}")
                sys.exit(1)

        msg = f"Running tests with coverage: {args.path}" if args.path else "Running tests with coverage..."
        Logger.info(msg)

        jobs = args.jobs if args.jobs else Config.default_jobs()
        output_dir = Config.build_coverage_dir()
        line_threshold = getattr(args, "fail_under_line", None) or Config.coverage_line_threshold()
        branch_threshold = getattr(args, "fail_under_branch", None) or Config.coverage_branch_threshold()
        function_threshold = getattr(args, "fail_under_function", None) or Config.coverage_function_threshold()

        if args.path:
            cmd = ["west", "twister", "-T", args.path]
        else:
            test_dirs = find_app_roots(tests=True, samples=False)
            if not test_dirs:
                Logger.err("No test directories found")
                sys.exit(1)
            test_args = [arg for d in test_dirs for arg in ["-T", d]]
            cmd = ["west", "twister"] + test_args

        cmd.extend(["-p", Config.default_board(), "-j", str(jobs), "-O", output_dir, "-C"])

        env = env_with(
            {"CMAKE_BUILD_PARALLEL_LEVEL": str(Config.ninja_jobs_per_build())}
        )
        rc = SubprocessRunner(env=env).run(cmd)

        if rc != 0:
            Logger.err("Tests failed")
            sys.exit(rc)

        Logger.info("Generating coverage report...")
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        repo_root = Path.cwd().resolve()

        gcovr_cmd = [
            GCOVR,
            "--root",
            repo_root.as_posix(),
            "--filter",
            r"^src/.*",
            "--exclude",
            r".*test.*",
            "--exclude",
            r".*sample.*",
            "--exclude",
            r".*build.*",
            "--html-details",
            str(output_path / "index.html"),
            "--txt",
            "--txt",
            str(output_path / "coverage.txt"),
            "--print-summary",
            "--fail-under-line",
            str(line_threshold),
            "--fail-under-branch",
            str(branch_threshold),
            "--fail-under-function",
            str(function_threshold),
            str(output_dir),
        ]

        rc = SubprocessRunner(timeout=300).run(gcovr_cmd)
        if rc != 0:
            Logger.err(
                f"Coverage below thresholds (L:{line_threshold}% B:{branch_threshold}% F:{function_threshold}%)"
            )
            sys.exit(rc)

        Logger.ok(
            f"Coverage thresholds met (L:{line_threshold}% B:{branch_threshold}% F:{function_threshold}%)"
        )
        Logger.ok(
            f'Coverage report: file://{(output_path / "index.html").absolute().as_posix()}'
        )
        Logger.ok(
            f'Text report:     file://{(output_path / "coverage.txt").absolute().as_posix()}'
        )

    def do_ci(self, args):
        """Run full CI pipeline"""
        Logger.info("Starting CI pipeline...")
        print()

        jobs_arg = ["-j", str(args.jobs)] if args.jobs else []
        verbose_arg = ["-v"] if args.verbose else []

        steps = [
            ("[1/6] Checking code format...", ["west", "spn", "format", "--check"]),
            (
                "[2/6] Building all samples...",
                ["west", "spn", "build"] + jobs_arg + verbose_arg,
            ),
            (
                "[3/6] Running all tests (baseline)...",
                ["west", "spn", "test"] + jobs_arg + verbose_arg,
            ),
            (
                "[4/6] Running all tests with ASAN...",
                ["west", "spn", "test", "--asan"] + jobs_arg + verbose_arg,
            ),
            (
                "[5/6] Running static analysis (clang-tidy)...",
                ["west", "spn", "tidy"] + jobs_arg,
            ),
            (
                "[6/6] Running all tests with coverage...",
                ["west", "spn", "coverage"] + jobs_arg,
            ),
        ]

        for step_msg, cmd in steps:
            Logger.info(step_msg)
            rc = SubprocessRunner(timeout=600).run(cmd)
            if rc != 0:
                Logger.err(f"CI step failed: {step_msg}")
                sys.exit(rc)
            print()

        Logger.ok("CI pipeline completed successfully")

    def do_run_sample(self, args):
        """Build and run sample with timeout"""
        cmd = ["west", "spn", "build", args.path, "-b", args.board]
        if args.asan:
            cmd.append("--asan")

        rc = SubprocessRunner().run(cmd)
        if rc != 0:
            sys.exit(rc)

        board = "native_sim" if args.asan else args.board
        base_dir = Config.build_samples_asan_dir() if args.asan else None
        build_dir = PathUtils.compute_build_dir(args.path, board, base_dir)

        Logger.info(f"Executing: {args.path} (timeout: {args.timeout}s)")
        rc = SubprocessRunner(timeout=args.timeout).run(
            ["west", "build", "-d", build_dir, "-t", "run"]
        )

        if rc == 0:
            Logger.ok(f"Execution completed: {args.path}")
        else:
            Logger.info(f"Execution ended (timeout/exit): {args.path}")
