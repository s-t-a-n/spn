#!/usr/bin/env python3
"""
Coverage summary script with colored output.
Parses gcovr JSON output and displays a nice summary.
"""

import json
import sys
from pathlib import Path


class Colors:
    GREEN = '\033[0;32m'
    RED = '\033[0;31m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    BOLD = '\033[1m'
    NC = '\033[0m'  # No Color


def format_percentage(covered, total):
    """Format coverage percentage with color."""
    if total == 0:
        return f"{Colors.YELLOW}N/A{Colors.NC}"

    percentage = (covered / total) * 100
    if percentage >= 90:
        color = Colors.GREEN
    elif percentage >= 70:
        color = Colors.YELLOW
    else:
        color = Colors.RED

    return f"{color}{percentage:.1f}%{Colors.NC}"


def format_count(covered, total):
    """Format coverage count."""
    return f"{covered}/{total}"


def analyze_coverage_data(coverage_data):
    """Analyze coverage data and compute totals."""
    files = coverage_data.get('files', [])

    total_lines = 0
    covered_lines = 0
    total_branches = 0
    covered_branches = 0
    total_functions = 0
    covered_functions = 0

    file_stats = []

    for file_data in files:
        filename = file_data.get('file', 'unknown')

        # Count lines
        lines = file_data.get('lines', [])
        file_total_lines = len(lines)
        file_covered_lines = sum(1 for line in lines if line.get('count', 0) > 0)

        # Count branches
        branches = []
        for line in lines:
            branches.extend(line.get('branches', []))
        file_total_branches = len(branches)
        file_covered_branches = sum(1 for branch in branches if branch.get('count', 0) > 0)

        # Count functions
        functions = file_data.get('functions', [])
        file_total_functions = len(functions)
        file_covered_functions = sum(1 for func in functions if func.get('execution_count', 0) > 0)

        file_stats.append({
            'filename': filename,
            'line_covered': file_covered_lines,
            'line_total': file_total_lines,
            'branch_covered': file_covered_branches,
            'branch_total': file_total_branches,
            'function_covered': file_covered_functions,
            'function_total': file_total_functions
        })

        total_lines += file_total_lines
        covered_lines += file_covered_lines
        total_branches += file_total_branches
        covered_branches += file_covered_branches
        total_functions += file_total_functions
        covered_functions += file_covered_functions

    return {
        'line_covered': covered_lines,
        'line_total': total_lines,
        'branch_covered': covered_branches,
        'branch_total': total_branches,
        'function_covered': covered_functions,
        'function_total': total_functions,
        'files': file_stats
    }


def print_coverage_summary(coverage_data):
    """Print a nice coverage summary."""
    stats = analyze_coverage_data(coverage_data)

    print(f"\n{Colors.BOLD}📊 SPN Module Coverage Summary{Colors.NC}")
    print("=" * 50)

    # Overall summary
    line_covered = stats['line_covered']
    line_total = stats['line_total']
    branch_covered = stats['branch_covered']
    branch_total = stats['branch_total']
    function_covered = stats['function_covered']
    function_total = stats['function_total']

    print(f"{Colors.BOLD}Overall Coverage:{Colors.NC}")
    print(f"  Lines:     {format_count(line_covered, line_total):>10} {format_percentage(line_covered, line_total):>8}")
    print(f"  Branches:  {format_count(branch_covered, branch_total):>10} {format_percentage(branch_covered, branch_total):>8}")
    print(f"  Functions: {format_count(function_covered, function_total):>10} {format_percentage(function_covered, function_total):>8}")

    # Per-file breakdown
    files = stats['files']
    if files:
        print(f"\n{Colors.BOLD}Per-File Breakdown:{Colors.NC}")
        print(f"{'File':<40} {'Lines':<12} {'Branches':<12} {'Functions':<12}")
        print("-" * 76)

        for file_data in sorted(files, key=lambda x: x['filename']):
            filename = file_data['filename']
            # Shorten filename for display
            display_name = filename
            if len(display_name) > 38:
                display_name = "..." + display_name[-35:]

            print(f"{display_name:<40} "
                  f"{format_percentage(file_data['line_covered'], file_data['line_total']):<12} "
                  f"{format_percentage(file_data['branch_covered'], file_data['branch_total']):<12} "
                  f"{format_percentage(file_data['function_covered'], file_data['function_total']):<12}")

    # Coverage recommendations
    line_percentage = (line_covered / line_total * 100) if line_total > 0 else 0
    branch_percentage = (branch_covered / branch_total * 100) if branch_total > 0 else 0

    print(f"\n{Colors.BOLD}Recommendations:{Colors.NC}")
    if line_percentage < 80:
        print(f"  {Colors.YELLOW}•{Colors.NC} Consider adding more tests to improve line coverage (target: 80%)")
    if branch_percentage < 70:
        print(f"  {Colors.YELLOW}•{Colors.NC} Add tests for edge cases to improve branch coverage (target: 70%)")
    if line_percentage >= 90 and branch_percentage >= 80:
        print(f"  {Colors.GREEN}•{Colors.NC} Excellent coverage! Keep up the good work.")

    print()


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <coverage.json>", file=sys.stderr)
        sys.exit(1)

    coverage_file = Path(sys.argv[1])
    if not coverage_file.exists():
        print(f"Error: Coverage file '{coverage_file}' not found", file=sys.stderr)
        sys.exit(1)

    try:
        with open(coverage_file, 'r') as f:
            coverage_data = json.load(f)

        print_coverage_summary(coverage_data)

    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in coverage file: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: Failed to process coverage file: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()