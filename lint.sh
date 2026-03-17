#!/bin/bash
# Run clang-tidy on project source files only (excludes third-party components)
#
# Usage:
#   ./lint.sh                     Run checks, output to warnings.txt
#   ./lint.sh --fix main/foo.cpp  Auto-fix a specific file
set -e

if [ "$1" = "--fix" ] && [ -n "$2" ]; then
    clang-tidy --config-file=.clang-tidy --fix -p build "$2"
    exit 0
fi

idf.py clang-check \
    --exclude-paths components/ \
    --run-clang-tidy-options "-header-filter='.*/main/.*'"

# Filter warnings.txt to only show lines from main/ (excluding generated parser files)
if [ -f warnings.txt ]; then
    grep -v -E '(/esp/|/esp-idf/|/\.espressif/|/components/|main/parser\.[ch])' warnings.txt > warnings_filtered.txt
    mv warnings_filtered.txt warnings.txt
fi
