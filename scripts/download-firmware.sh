#!/usr/bin/env bash
set -euo pipefail

# Download built firmware from GitHub Actions, organized by branch name.
# Usage:
#   ./scripts/download-firmware.sh          # current branch only
#   ./scripts/download-firmware.sh --all    # all remote branches

cd "$(git rev-parse --show-toplevel)"

# Newest completed run for this branch that still has unexpired artifacts.
# Deliberately NOT filtered on --status success: a run is marked failed if any
# single target fails (e.g. settings_reset), even though the other targets
# uploaded perfectly good firmware. Artifacts also expire after 90 days, so the
# newest run with artifacts is not always the newest run.
find_run() {
  local branch="$1" id
  for id in $(gh run list --branch "$branch" --workflow "Build and Draw" \
                --status completed --limit 15 --json databaseId --jq '.[].databaseId'); do
    if [[ $(gh api "repos/{owner}/{repo}/actions/runs/$id/artifacts" \
              --jq '[.artifacts[] | select(.expired == false)] | length') -gt 0 ]]; then
      echo "$id"
      return 0
    fi
  done
  return 1
}

if [[ "${1:-}" == "--all" ]]; then
  git fetch --prune
  branches=$(git branch -r | sed 's|origin/||' | grep -v 'HEAD' | xargs)
else
  branches=$(git branch --show-current)
fi

for branch in $branches; do
  if ! run_id=$(find_run "$branch"); then
    echo "SKIP $branch — no completed run with unexpired artifacts"
    continue
  fi

  echo "Downloading $branch (run $run_id)..."

  # Stage into a temp dir first: a failed download must not destroy the
  # firmware already on disk.
  staging=$(mktemp -d)
  trap 'rm -rf "$staging"' EXIT
  if ! gh run download "$run_id" --dir "$staging"; then
    echo "  FAILED — keeping existing firmware/$branch" >&2
    rm -rf "$staging"
    continue
  fi

  # Normalize the layout. When the "Merge Output Artifacts" job runs we get a
  # single firmware/ dir; when it is skipped we get one artifact-*/ dir per
  # target. Flatten both into firmware/<branch>/firmware/ so flash-go60.sh
  # finds the .uf2 files either way.
  mkdir -p "$staging/firmware"
  find "$staging" -name '*.uf2' -not -path "$staging/firmware/*" -exec mv {} "$staging/firmware/" \;
  find "$staging" -mindepth 1 -maxdepth 1 -type d -not -name firmware -exec rm -rf {} +

  rm -rf "firmware/$branch"
  mkdir -p "firmware/$branch"
  mv "$staging"/* "firmware/$branch/"
  rm -rf "$staging"
  trap - EXIT

  echo "  OK — $(find "firmware/$branch" -name '*.uf2' | wc -l | tr -d ' ') .uf2 files"
done

echo "Done."
