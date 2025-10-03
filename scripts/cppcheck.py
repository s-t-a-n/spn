#!/usr/bin/env python3
"""
Comprehensive cppcheck static analysis wrapper.
Supports MISRA, threadsafety, y2038, and other addons with proper config handling.
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Dict, Set, Optional, Tuple
from dataclasses import dataclass, field


class Colors:
    GREEN = '\033[0;32m'
    RED = '\033[0;31m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    BOLD = '\033[1m'
    NC = '\033[0m'


@dataclass
class CppcheckConfig:
    """Configuration for cppcheck analysis."""
    binary: str = "cppcheck"
    std: str = "c++20"
    check_level: str = "normal"
    libraries: List[str] = field(default_factory=lambda: ["posix"])
    suppressions: List[str] = field(default_factory=list)
    dump_flags: List[str] = field(default_factory=lambda: ["--quiet", "--enable=all", "--inline-suppr"])
    extra_args: List[str] = field(default_factory=list)


@dataclass
class AddonConfig:
    """Configuration for a cppcheck addon."""
    name: str
    enabled: bool = True
    script_path: Optional[str] = None
    extra_args: List[str] = field(default_factory=list)
    suppress_rules: List[str] = field(default_factory=list)


@dataclass
class AnalysisResults:
    """Results from cppcheck analysis."""
    total_files: int = 0
    files_analyzed: int = 0
    files_with_violations: int = 0
    total_violations: int = 0
    violations_by_addon: Dict[str, int] = field(default_factory=dict)
    violations_by_rule: Dict[str, int] = field(default_factory=dict)
    violations_by_file: Dict[str, List[str]] = field(default_factory=dict)
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)


def load_config(config_file: Path) -> Tuple[CppcheckConfig, Dict[str, AddonConfig]]:
    """Load configuration from JSON file."""
    try:
        with open(config_file, 'r') as f:
            config = json.load(f)
    except Exception as e:
        print(f"{Colors.YELLOW}Warning: Could not load config file: {e}{Colors.NC}")
        return CppcheckConfig(), {}

    cppcheck_cfg = CppcheckConfig()
    addons = {}

    if 'config' in config:
        cfg = config['config']

        if 'cppcheck' in cfg:
            cc = cfg['cppcheck']
            cppcheck_cfg.binary = cc.get('binary', cppcheck_cfg.binary)
            cppcheck_cfg.std = cc.get('std', cppcheck_cfg.std)
            cppcheck_cfg.check_level = cc.get('check_level', cppcheck_cfg.check_level)
            cppcheck_cfg.libraries = cc.get('libraries', cppcheck_cfg.libraries)
            cppcheck_cfg.suppressions = cc.get('suppressions', cppcheck_cfg.suppressions)
            cppcheck_cfg.dump_flags = cc.get('dump_flags', cppcheck_cfg.dump_flags)
            cppcheck_cfg.extra_args = cc.get('extra_args', cppcheck_cfg.extra_args)

        if 'misra_addon' in cfg:
            ma = cfg['misra_addon']
            addons['misra'] = AddonConfig(
                name='misra',
                enabled=ma.get('enabled', True),
                script_path=ma.get('script', '/usr/lib/x86_64-linux-gnu/cppcheck/addons/misra.py'),
                extra_args=ma.get('extra_args', []),
                suppress_rules=ma.get('suppress_rules', [])
            )

        if 'threadsafety_addon' in cfg:
            ta = cfg['threadsafety_addon']
            addons['threadsafety'] = AddonConfig(
                name='threadsafety',
                enabled=ta.get('enabled', True),
                script_path=ta.get('script', '/usr/lib/x86_64-linux-gnu/cppcheck/addons/threadsafety.py'),
                extra_args=ta.get('extra_args', []),
                suppress_rules=ta.get('suppress_rules', [])
            )

        if 'y2038_addon' in cfg:
            ya = cfg['y2038_addon']
            addons['y2038'] = AddonConfig(
                name='y2038',
                enabled=ya.get('enabled', True),
                script_path=ya.get('script', '/usr/lib/x86_64-linux-gnu/cppcheck/addons/y2038.py'),
                extra_args=ya.get('extra_args', []),
                suppress_rules=ya.get('suppress_rules', [])
            )

    return cppcheck_cfg, addons


def find_source_files(src_dir: Path, exclude_patterns: List[str] = None) -> List[Path]:
    """Find all C++ source files, excluding specified patterns."""
    if exclude_patterns is None:
        exclude_patterns = ['build', 'test', '.dump']

    source_files = []
    patterns = ['**/*.cpp', '**/*.hpp', '**/*.h', '**/*.c']

    for pattern in patterns:
        for file in src_dir.glob(pattern):
            if any(excl in str(file) for excl in exclude_patterns):
                continue
            source_files.append(file)

    return sorted(source_files)


def create_suppressions_file(suppressions: List[str]) -> Optional[Path]:
    """Create a temporary suppressions file for cppcheck."""
    if not suppressions:
        return None

    try:
        temp_file = tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt')
        for supp in suppressions:
            temp_file.write(f"{supp}\n")
        temp_file.close()
        return Path(temp_file.name)
    except Exception as e:
        print(f"{Colors.YELLOW}Warning: Could not create suppressions file: {e}{Colors.NC}")
        return None


def run_cppcheck_dump(file: Path, config: CppcheckConfig, verbose: bool) -> Tuple[Optional[Path], Optional[str]]:
    """Run cppcheck to generate dump file for a source file.

    Returns the dump file path on success and an error message if the invocation failed.
    """
    if verbose:
        print(f"  Analyzing: {file}")

    try:
        dump_cmd = [config.binary, '--dump']

        dump_cmd.extend(config.dump_flags)

        if config.std:
            dump_cmd.append(f'--std={config.std}')

        if config.check_level and config.check_level != 'normal':
            dump_cmd.append(f'--check-level={config.check_level}')

        for lib in config.libraries:
            dump_cmd.append(f'--library={lib}')

        for supp in config.suppressions:
            dump_cmd.append(f'--suppress={supp}')

        dump_cmd.extend(config.extra_args)
        dump_cmd.append(str(file))

        if verbose:
            print(f"    Command: {' '.join(dump_cmd)}")

        result = subprocess.run(dump_cmd, capture_output=True, text=True, timeout=120)

        dump_file = file.with_suffix(file.suffix + '.dump')

        if result.returncode != 0:
            message_lines = [
                f"cppcheck failed for {file} (exit code {result.returncode})."
            ]
            if result.stdout.strip():
                message_lines.append(f"Stdout: {result.stdout.strip()}")
            if result.stderr.strip():
                message_lines.append(f"Stderr: {result.stderr.strip()}")
            full_message = "\n".join(message_lines)
            print(f"    {Colors.RED}{full_message}{Colors.NC}")
            return None, full_message

        if verbose:
            print(f"    Checking dump file: {dump_file}, exists={dump_file.exists()}")

        if dump_file.exists():
            return dump_file, None

        message = f"cppcheck did not generate dump file for {file}"
        print(f"    {Colors.RED}{message}{Colors.NC}")
        return None, message

    except subprocess.TimeoutExpired:
        message = f"Timeout analyzing {file}"
        print(f"  {Colors.RED}{message}{Colors.NC}")
        return None, message
    except Exception as e:
        message = f"Error generating dump for {file}: {e}"
        print(f"  {Colors.RED}{message}{Colors.NC}")
        return None, message


def run_addon(dump_file: Path, addon: AddonConfig, verbose: bool) -> List[str]:
    """Run a cppcheck addon on a dump file."""
    if not addon.enabled or not addon.script_path:
        return []

    script_path = Path(addon.script_path)
    if not script_path.exists():
        if verbose:
            print(f"    {Colors.YELLOW}Addon {addon.name} script not found: {script_path}{Colors.NC}")
        return []

    try:
        addon_cmd = ['python3', str(script_path)]
        addon_cmd.extend(addon.extra_args)
        addon_cmd.append(str(dump_file))

        result = subprocess.run(addon_cmd, capture_output=True, text=True, timeout=60)

        violations = []
        for line in result.stdout.splitlines() + result.stderr.splitlines():
            line = line.strip()
            if not line:
                continue

            skip = False
            for rule in addon.suppress_rules:
                if rule in line:
                    skip = True
                    break

            if not skip and (addon.name.lower() in line.lower() or 'rule' in line.lower() or
                           'warning' in line.lower() or 'error' in line.lower()):
                violations.append(f"[{addon.name}] {line}")

        return violations

    except subprocess.TimeoutExpired:
        return [f"[{addon.name}] Timeout analyzing {dump_file.parent.name}/{dump_file.name}"]
    except Exception as e:
        return [f"[{addon.name}] Error: {e}"]


def analyze_files(files: List[Path], cppcheck_cfg: CppcheckConfig, addons: Dict[str, AddonConfig],
                 verbose: bool, detailed: bool) -> AnalysisResults:
    """Analyze source files with cppcheck and addons."""
    results = AnalysisResults(total_files=len(files))

    if not files:
        print(f"{Colors.YELLOW}No source files found{Colors.NC}")
        return results

    enabled_addons = {name: cfg for name, cfg in addons.items() if cfg.enabled}

    print(f"{Colors.BLUE}Running cppcheck analysis on {len(files)} files...{Colors.NC}")
    if enabled_addons:
        print(f"{Colors.BLUE}Enabled addons: {', '.join(enabled_addons.keys())}{Colors.NC}")

    for file in files:
        dump_file, error = run_cppcheck_dump(file, cppcheck_cfg, verbose)

        if verbose:
            print(f"  Dump result: {dump_file}")

        if error:
            results.errors.append(error)
            break

        if not dump_file:
            continue

        results.files_analyzed += 1
        file_violations = []

        for addon_name, addon_cfg in enabled_addons.items():
            violations = run_addon(dump_file, addon_cfg, verbose)
            if violations:
                file_violations.extend(violations)
                results.violations_by_addon[addon_name] = results.violations_by_addon.get(addon_name, 0) + len(violations)

                for violation in violations:
                    for part in violation.split():
                        if 'rule' in part.lower() or (addon_name in part.lower() and '-' in part):
                            rule_id = part.strip('[]():,')
                            results.violations_by_rule[rule_id] = results.violations_by_rule.get(rule_id, 0) + 1

        if file_violations:
            results.files_with_violations += 1
            results.violations_by_file[str(file)] = file_violations
            results.total_violations += len(file_violations)

            if detailed:
                print(f"\n{Colors.YELLOW}{file}{Colors.NC}")
                for violation in file_violations:
                    print(f"  {violation}")

        if dump_file.exists():
            dump_file.unlink()

    return results


def print_summary(results: AnalysisResults):
    """Print comprehensive analysis summary."""
    print(f"\n{Colors.BOLD}Static Analysis Summary{Colors.NC}")
    print("=" * 70)

    files_analyzed = results.files_analyzed
    files_with_violations = results.files_with_violations
    total_violations = results.total_violations

    compliant_files = files_analyzed - files_with_violations
    compliance_rate = (compliant_files / files_analyzed * 100) if files_analyzed > 0 else 0

    print(f"\n{Colors.BOLD}Overall Statistics:{Colors.NC}")
    print(f"  Total files found:     {results.total_files}")
    print(f"  Files analyzed:        {files_analyzed}")
    print(f"  Files with violations: {files_with_violations}")
    print(f"  Compliant files:       {compliant_files}")

    if compliance_rate >= 90:
        color = Colors.GREEN
    elif compliance_rate >= 70:
        color = Colors.YELLOW
    else:
        color = Colors.RED

    print(f"  Compliance rate:       {color}{compliance_rate:.1f}%{Colors.NC}")
    print(f"  Total violations:      {total_violations}")

    if results.violations_by_addon:
        print(f"\n{Colors.BOLD}Violations by Addon:{Colors.NC}")
        for addon, count in sorted(results.violations_by_addon.items(), key=lambda x: x[1], reverse=True):
            print(f"  {addon:15s}: {count:4d}")

    if results.violations_by_rule:
        print(f"\n{Colors.BOLD}Top Violated Rules:{Colors.NC}")
        sorted_rules = sorted(results.violations_by_rule.items(), key=lambda x: x[1], reverse=True)
        for rule, count in sorted_rules[:10]:
            print(f"  {rule:30s}: {count:3d}")

    if results.violations_by_file:
        print(f"\n{Colors.BOLD}Files with Most Violations:{Colors.NC}")
        sorted_files = sorted(results.violations_by_file.items(), key=lambda x: len(x[1]), reverse=True)
        for file, violations in sorted_files[:10]:
            display_name = file
            if len(display_name) > 55:
                display_name = "..." + display_name[-52:]
            print(f"  {display_name:55s}: {len(violations):3d}")

    if results.errors:
        print(f"\n{Colors.YELLOW}Errors:{Colors.NC}")
        for error in results.errors[:5]:
            print(f"  {error}")
        if len(results.errors) > 5:
            print(f"  ... and {len(results.errors) - 5} more")

    if results.warnings:
        print(f"\n{Colors.YELLOW}Warnings:{Colors.NC}")
        for warning in results.warnings[:5]:
            print(f"  {warning}")

    print(f"\n{Colors.BOLD}Notes:{Colors.NC}")
    print(f"  {Colors.YELLOW}*{Colors.NC} Multiple addons active (MISRA, threadsafety, y2038)")
    print(f"  {Colors.YELLOW}*{Colors.NC} Review violations and document justified deviations")
    print(f"  {Colors.YELLOW}*{Colors.NC} Use --detailed to see all violations inline")

    if total_violations == 0 and files_analyzed > 0:
        print(f"\n{Colors.GREEN}Excellent! No violations detected.{Colors.NC}")
    elif compliance_rate >= 90:
        print(f"\n{Colors.GREEN}Good compliance rate! Address remaining violations.{Colors.NC}")
    else:
        print(f"\n{Colors.YELLOW}Consider addressing violations or documenting deviations.{Colors.NC}")

    print()


def main():
    parser = argparse.ArgumentParser(
        description='Comprehensive cppcheck static analysis with addon support',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --config scripts/cppcheck_config.json
  %(prog)s --config scripts/cppcheck_config.json --verbose --detailed
  %(prog)s --config scripts/cppcheck_config.json --fail-on-violations
        """
    )
    parser.add_argument('--src-dir', type=Path, default=Path('src'),
                       help='Source directory to scan (default: src)')
    parser.add_argument('--config', type=Path, default=Path('scripts/cppcheck_config.json'),
                       help='Configuration file (default: scripts/cppcheck_config.json)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Verbose output')
    parser.add_argument('--detailed', '-d', action='store_true',
                       help='Show violations inline during analysis')
    parser.add_argument('--fail-on-violations', action='store_true',
                       help='Exit with error code if violations found')
    parser.add_argument('--exclude', type=str, nargs='+', default=['build', 'test'],
                       help='Patterns to exclude (default: build test)')

    args = parser.parse_args()

    if not args.src_dir.exists():
        print(f"{Colors.RED}Error: Source directory '{args.src_dir}' not found{Colors.NC}", file=sys.stderr)
        sys.exit(1)

    cppcheck_cfg, addons = load_config(args.config)

    source_files = find_source_files(args.src_dir, args.exclude)

    if not source_files:
        print(f"{Colors.YELLOW}No source files found in {args.src_dir}{Colors.NC}")
        sys.exit(0)

    results = analyze_files(source_files, cppcheck_cfg, addons, args.verbose, args.detailed)

    print_summary(results)

    if results.errors:
        sys.exit(2)

    if args.fail_on_violations and results.total_violations > 0:
        sys.exit(1)

    sys.exit(0)


if __name__ == '__main__':
    main()
