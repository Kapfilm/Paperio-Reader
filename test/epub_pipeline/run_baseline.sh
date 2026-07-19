#!/usr/bin/env bash
# Host metrics baseline for the pipeline migration (plan Phase 0 step 4).
# Runs epub_pipeline_dump --bench over the synthetic corpus plus moby-dick,
# cold then warm, and prints a markdown table for docs/pipeline-baseline-*.md.
#
# Usage: run_baseline.sh <path-to-epub_pipeline_dump> [output.md]
set -euo pipefail

DUMP="${1:?usage: run_baseline.sh <epub_pipeline_dump> [output.md]}"
OUT="${2:-/dev/stdout}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

books=("$REPO_ROOT"/test/epubs/*.epub "$REPO_ROOT"/test/fixtures/moby-dick.epub)

{
  echo "| book | spines | pages | cold ms | warm ms | peak heap KB | cache KB |"
  echo "|---|---:|---:|---:|---:|---:|---:|"
  for epub in "${books[@]}"; do
    name="$(basename "$epub" .epub)"
    cache="$WORK/$name"
    cold_err="$WORK/$name.cold.err"
    warm_err="$WORK/$name.warm.err"
    dump="$WORK/$name.dump"

    "$DUMP" "$epub" "$cache" --bench > "$dump" 2> "$cold_err"
    "$DUMP" "$epub" "$cache" --bench > /dev/null 2> "$warm_err"

    spines="$(grep -c '^SPINE' "$dump")"
    pages="$(grep -c '^ PAGE' "$dump")"
    cold_us="$(sed -n 's/.*pipeline_ok time=\([0-9]*\)us.*/\1/p' "$cold_err")"
    warm_us="$(sed -n 's/.*pipeline_ok time=\([0-9]*\)us.*/\1/p' "$warm_err")"
    peak_b="$(sed -n 's/.*heap_peak=\([0-9]*\)B.*/\1/p' "$cold_err")"
    cache_b="$(sed -n 's/.*cache_bytes=\([0-9]*\).*/\1/p' "$cold_err")"

    printf "| %s | %s | %s | %d.%03d | %d.%03d | %d | %d |\n" \
      "$name" "$spines" "$pages" \
      $((cold_us / 1000)) $((cold_us % 1000)) \
      $((warm_us / 1000)) $((warm_us % 1000)) \
      $((peak_b / 1024)) $((cache_b / 1024))
  done
} > "$OUT"
