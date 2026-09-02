#!/bin/sh
# Verifies the topic coverage contract of docs/topics.tsv: every keyword
# listed for a document must appear in it (case-insensitive, fixed string).
# This catches documentation that lags behind a feature, which the API and
# agent-guide checks (symbols only) cannot see.
set -eu
missing=0
while IFS="$(printf '\t')" read -r path keywords; do
    case $path in ''|'#'*) continue ;; esac
    if [ ! -f "$path" ]; then
        printf 'doc-topics-check: %s does not exist\n' "$path" >&2
        missing=$((missing + 1))
        continue
    fi
    old_ifs=$IFS
    IFS='|'
    for keyword in $keywords; do
        if ! grep -qiF -- "$keyword" "$path"; then
            printf '%s lacks topic: %s\n' "$path" "$keyword" >&2
            missing=$((missing + 1))
        fi
    done
    IFS=$old_ifs
done < docs/topics.tsv
if [ "$missing" -ne 0 ]; then
    printf '%s missing topic(s); update the document or docs/topics.tsv\n' "$missing" >&2
    exit 1
fi
printf 'doc-topics-check: ok\n'
