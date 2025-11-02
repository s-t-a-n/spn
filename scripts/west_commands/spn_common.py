import multiprocessing
import os
import subprocess
import sys
from pathlib import Path


def env_with(overrides):
    """Create environment with overrides"""
    env = os.environ.copy()
    env.update(overrides)
    return env


def find_source_files(tests=False, samples=False):
    """Find all C++ source files in src/"""
    files = []
    for module in Path("src").iterdir():
        if not module.is_dir():
            continue
        for pattern in ["**/*.c", "**/*.cpp", "**/*.h", "**/*.hpp"]:
            for f in module.glob(pattern):
                if "build" in f.parts:
                    continue
                if not tests and "tests" in f.parts:
                    continue
                if not samples and "samples" in f.parts:
                    continue
                files.append(str(f))
    return sorted(files)


def find_app_roots(tests=True, samples=True):
    """Find test/sample directories in src/"""
    dirs = []
    for module in Path("src").iterdir():
        if not module.is_dir():
            continue
        if tests:
            tests_dir = module / "tests"
            if tests_dir.is_dir():
                dirs.append(str(tests_dir))
        if samples:
            samples_dir = module / "samples"
            if samples_dir.is_dir():
                dirs.append(str(samples_dir))
    return sorted(dirs)


def find_next_output_dir(base_dir):
    """Find next available output directory (matches twister's auto-increment)"""
    if not os.path.exists(base_dir):
        return base_dir

    for i in range(1, 100):
        candidate = f"{base_dir}.{i}"
        if not os.path.exists(candidate):
            return candidate

    raise RuntimeError(f"Too many '{base_dir}.*' directories")


class Logger:
    """Logging utilities"""

    GREEN = "\033[1;32m"
    RED = "\033[1;31m"
    YELLOW = "\033[1;33m"
    BLUE = "\033[1;34m"
    NC = "\033[0m"

    @staticmethod
    def info(msg):
        print(f"{Logger.BLUE}[ INFO ] {msg}{Logger.NC}")

    @staticmethod
    def ok(msg):
        print(f"{Logger.GREEN}[  OK  ] {msg}{Logger.NC}")

    @staticmethod
    def wrn(msg):
        print(f"{Logger.YELLOW}[  WRN ] {msg}{Logger.NC}")

    @staticmethod
    def err(msg):
        print(f"{Logger.RED}[  ERR ] {msg}{Logger.NC}")


class Config:
    """Configuration and default values"""

    @staticmethod
    def default_board():
        """Default board for builds"""
        return "native_sim"

    @staticmethod
    def default_jobs():
        """Default twister job count"""
        try:
            return max(1, multiprocessing.cpu_count() // 2)
        except NotImplementedError:
            return 2

    @staticmethod
    def ninja_jobs_per_build():
        """Ninja jobs per build to avoid oversubscription"""
        try:
            return max(1, multiprocessing.cpu_count() // Config.default_jobs())
        except NotImplementedError:
            return 2

    @staticmethod
    def build_samples_dir():
        """Build directory for samples"""
        return "build_samples"

    @staticmethod
    def build_tests_dir():
        """Build directory for tests"""
        return "build_tests"

    @staticmethod
    def build_tests_asan_dir():
        """Build directory for ASAN/UBSAN tests"""
        return "build_tests_asan"

    @staticmethod
    def build_samples_asan_dir():
        """Build directory for ASAN/UBSAN samples"""
        return "build_samples_asan"

    @staticmethod
    def build_compile_db_dir():
        """Build directory for compile database"""
        return "build_compile_db"

    @staticmethod
    def build_coverage_dir():
        """Coverage output directory"""
        return "build_coverage"

    @staticmethod
    def coverage_line_threshold():
        """Minimum line coverage percentage for CI"""
        return 0

    @staticmethod
    def coverage_branch_threshold():
        """Minimum branch coverage percentage for CI"""
        return 0

    @staticmethod
    def coverage_function_threshold():
        """Minimum function coverage percentage for CI"""
        return 0

    @staticmethod
    def asan_configs():
        """ASAN/UBSAN configs"""
        return [
            "CONFIG_ASAN=y",
            "CONFIG_UBSAN=y",
            "CONFIG_STACK_SENTINEL=y",
            "CONFIG_THREAD_STACK_INFO=y",
            "CONFIG_ASSERT=y",
            "CONFIG_ASSERT_LEVEL=2",
            "CONFIG_DEBUG=y",
            "CONFIG_EXCEPTION_STACK_TRACE=y",
        ]


class PathUtils:
    """Path manipulation and validation utilities"""

    @staticmethod
    def sanitize(path):
        """Convert path to filesystem safe name"""
        return path.replace("/", "_").replace("\\", "_")

    @staticmethod
    def compute_build_dir(path, board, base_dir=None):
        """Compute build directory for given path and board"""
        if base_dir is None:
            if "tests" in Path(path).parts:
                base_dir = Config.build_tests_dir()
            elif "samples" in Path(path).parts:
                base_dir = Config.build_samples_dir()
            else:
                raise ValueError(f"Cannot determine build directory for path: {path}")
        build_name = PathUtils.sanitize(path)
        board_name = PathUtils.sanitize(board)
        return str(Path(base_dir) / f"{build_name}-{board_name}")

    @staticmethod
    def validate_project(path):
        """Validate project path"""
        if not os.path.isdir(path):
            Logger.err(f"Directory {path} does not exist")
            sys.exit(1)

        if not os.path.isfile(os.path.join(path, "CMakeLists.txt")):
            Logger.err(f"{path} is not a valid sample/test (no CMakeLists.txt)")
            sys.exit(1)


class TwisterOpts:
    """Build twister options"""

    @staticmethod
    def build(jobs=None, verbose=False):
        """Build twister options list from parameters"""
        opts = []

        if jobs is None:
            jobs = Config.default_jobs()
        opts.extend(["-j", str(jobs)])

        if verbose:
            opts.append("-v")

        return opts


class SubprocessRunner:
    """Execute subprocess with timeout and guaranteed cleanup as failing runs tend to stick around as zombies"""

    def __init__(self, timeout=None, env=None, **kwargs):
        """Create runner"""
        self.timeout = timeout
        self.env = env
        self.popen_kwargs = kwargs
        self.process = None

    def run(self, cmd):
        """Run command, ensure cleanup, return exit code"""
        try:
            self.process = subprocess.Popen(cmd, env=self.env, **self.popen_kwargs)
            returncode = self.process.wait(timeout=self.timeout)
            return returncode
        except subprocess.TimeoutExpired:
            self._cleanup()
            Logger.err(f"Command timed out after {self.timeout}s")
            return 1
        except KeyboardInterrupt:
            self._cleanup()
            raise
        finally:
            self._cleanup()

    def _cleanup(self):
        """Kill launched runner process"""
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
