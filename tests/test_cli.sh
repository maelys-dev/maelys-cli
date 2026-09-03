#!/bin/sh
# End-to-end contract tests for maelys-hello (product CLI) and maelys
# (dispatcher). usage: tests/test_cli.sh BUILD_BIN_DIR
set -eu
bin=$(cd "$1" && pwd)
hello="$bin/maelys-hello"
maelys="$bin/maelys"
work=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-e2e.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0

check() {
    if eval "$2"; then
        printf 'PASS %s\n' "$1"
    else
        printf 'FAIL %s\n' "$1" >&2
        failures=$((failures + 1))
    fi
}

run() { # run NAME -- command...; captures $out $err $code
    name=$1
    shift
    set +e
    "$@" >"$work/out" 2>"$work/err"
    code=$?
    set -e
    out=$(cat "$work/out")
    err=$(cat "$work/err")
}

json_ok() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import json,sys; json.load(sys.stdin)' <"$1"
    else
        grep -q '^{' "$1"
    fi
}

run version "$hello" --version
check "version text" '[ "$code" = 0 ] && [ "$out" = "maelys-hello 0.1.0" ] && [ -z "$err" ]'

run version-json "$hello" version --format json --compact --non-interactive
check "version json envelope on stdout only" '[ "$code" = 0 ] && [ -z "$err" ] && json_ok "$work/out" && printf "%s" "$out" | grep -q "\"command\":\"version\",\"ok\":true,\"exitCode\":0"'

run describe "$hello" describe --format json --compact --non-interactive
check "describe is valid json and silent on stderr" '[ "$code" = 0 ] && [ -z "$err" ] && json_ok "$work/out"'
check "describe usage equals synopsis" 'printf "%s" "$out" | grep -q "\"usage\":\"note write FILE --content TEXT \[--replace\] \[--apply\]\",.*\"synopsis\":\"note write FILE --content TEXT \[--replace\] \[--apply\]\""'

run help "$hello" help
check "help lists commands" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "note write FILE --content TEXT" && printf "%s" "$out" | grep -q "AGENT CONTRACT"'

run greet "$hello" greet world --shout --times 2
check "greet text" '[ "$code" = 0 ] && [ "$out" = "HELLO, WORLD!
HELLO, WORLD!" ]'

run greet-json "$hello" greet world --json --compact
check "greet json" '[ "$out" = "{\"schemaVersion\":2,\"contract\":\"agent-cli/v2\",\"command\":\"greet\",\"ok\":true,\"exitCode\":0,\"data\":{\"greeting\":\"Hello, world!\",\"times\":1}}" ]'

run unknown "$hello" greet world --loud --json --compact
check "unknown option fails with stderr envelope and empty stdout" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\"ok\":false,\"exitCode\":1,\"error\":{\"code\":\"VALIDATION_FAILED\""'

run range "$hello" greet world --times 11
check "range refused in text mode" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "maelys-hello: \[VALIDATION_FAILED\] Option --times expects an unsigned integer between 1 and 10"'

run limits "$hello" limits --memory 4M --wall-time 2m --level high --offset -7 --tag a --tag b --json --compact
check "typed values" 'printf "%s" "$out" | grep -q "\"data\":{\"memory\":4194304,\"wallTimeMs\":120000,\"level\":\"high\",\"offset\":-7,\"tags\":\[\"a\",\"b\"\]}"'

run duplicate "$hello" limits --memory 1M --memory 2M
check "duplicate refused" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "only once"'

run requires "$hello" limits --strict
check "requires enforced" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q -- "--strict requires --level"'

note="$work/note.txt"
run plan "$hello" note write "$note" --content hello --json --compact
check "plan writes nothing" '[ "$code" = 0 ] && [ ! -e "$note" ] && printf "%s" "$out" | grep -q "\"mode\":\"plan\",\"changed\":false"'

run dryrun "$hello" note write "$note" --content hello --dry-run
check "legacy --dry-run refused" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "Add --apply only after reviewing"'

run apply "$hello" note write "$note" --content hello --apply --json --compact
check "apply writes once" '[ "$code" = 0 ] && [ "$(cat "$note")" = "hello" ] && printf "%s" "$out" | grep -q "\"mode\":\"apply\",\"changed\":true"'

run reapply "$hello" note write "$note" --content again --apply --json --compact
check "existing target is a precondition failure" '[ "$code" = 1 ] && [ "$(cat "$note")" = "hello" ] && printf "%s" "$err" | grep -q "\"code\":\"PRECONDITION_FAILED\""'

run replace "$hello" note write "$note" --content again --replace --apply
check "replace overwrites atomically" '[ "$code" = 0 ] && [ "$(cat "$note")" = "again" ]'

run records "$hello" list --limit 3 --format jsonl
check "jsonl records" '[ "$code" = 0 ] && [ "$out" = "{\"index\":0,\"name\":\"alpha\"}
{\"index\":1,\"name\":\"beta\"}
{\"index\":2,\"name\":\"gamma\"}" ]'

run records-json "$hello" list --limit 2 --json --compact
check "json records envelope" 'printf "%s" "$out" | grep -q "\"data\":{\"count\":2,\"records\":\[{\"index\":0,\"name\":\"alpha\"},{\"index\":1,\"name\":\"beta\"}\]}"'

run jsonl-refused "$hello" greet world --format jsonl
check "jsonl refused for envelope commands" '[ "$code" = 1 ]'

private="$work/private"
printf 'x' >"$private"
chmod 0600 "$private"
run check-ok "$hello" check "$private" --json --compact
check "validation report exit 0" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"valid\":true"'
chmod 0644 "$private"
run check-bad "$hello" check "$private" --json --compact
check "validation report exit 2 on stdout" '[ "$code" = 2 ] && [ -z "$err" ] && printf "%s" "$out" | grep -q "\"ok\":true,\"exitCode\":2,\"data\":{\"valid\":false"'

run env "$hello" env show --env A=1 --env PATH --json --compact
check "environment overlay" 'printf "%s" "$out" | grep -q "\"name\":\"A\",\"value\":\"1\"" && printf "%s" "$out" | grep -q "\"name\":\"PATH\""'

run stream "$hello" run /bin/sh -c 'echo streamed; exit 3'
check "stream relays stdout and exit code" '[ "$code" = 3 ] && [ "$out" = "streamed" ] && [ -z "$err" ]'

run stream-dd "$hello" run -- /bin/sh -c 'echo --dashdash'
check "-- separates operands" '[ "$code" = 0 ] && [ "$out" = "--dashdash" ]'

run stream-flags "$hello" run /bin/sh --json
check "stream refuses rendering flags" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "stream command"'

helper="$bin/maelys-hello-image"
printf '#!/bin/sh\nprintf "%%s\\n" "$*"\nexit 5\n' >"$helper"
chmod 0755 "$helper"
run delegate "$hello" image inspect --platform linux/arm64 --json
check "delegate passes arguments verbatim and keeps exit code" '[ "$code" = 5 ] && [ "$out" = "inspect --platform linux/arm64 --json" ]'
rm -f "$helper"
run delegate-missing "$hello" image inspect
check "missing delegate reported" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[NOT_FOUND\]"'

run completion "$hello" completion bash
check "bash completion shim" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "complete -o filenames -F _maelys_hello_complete maelys-hello"'
run complete-words "$hello" __complete -- note ""
check "completion of command words" '[ "$out" = "write" ]'
run complete-option "$hello" __complete -- limits --level ""
check "completion of choices" '[ "$out" = "low
high" ]'
run env-format env MAELYS_CLI_FORMAT=json "$hello" greet x --times 99
check "MAELYS_CLI_FORMAT shapes the failure envelope" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\": \"VALIDATION_FAILED\""'
run env-stream env MAELYS_CLI_FORMAT=json "$hello" run /bin/sh -c "echo plain"
check "MAELYS_CLI_FORMAT leaves stream stdout untouched" '[ "$code" = 0 ] && [ "$out" = "plain" ]'

if command -v python3 >/dev/null 2>&1; then
    printf 'Contrat commun : `agent-cli/v2`.\n' >"$work/intro.md"
    run reference python3 "$(dirname "$0")/../tools/generate_cli_reference.py" --build "$bin" \
        --markdown "$work/ref.md" --json "$work/ref.json" --title "Référence CLI" \
        --intro-file "$work/intro.md" --columns "Identifiant|Usage|Effet|Sortie|But" \
        --global-label "Options globales :" maelys-hello
    run neutral python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import generate_cli_reference as g
data = {"commands": [{"id": "a", "available": False, "unavailableReason": "linux only"},
                     {"id": "b", "available": False, "unavailableReason": "linux only"},
                     {"id": "c", "available": True}]}
g.neutralize(data, {"a"})
assert data["commands"][0] == {"id": "a", "available": True}, data
assert data["commands"][1]["available"] is False, data
g.neutralize(data, None)
assert all(c["available"] is True and "unavailableReason" not in c for c in data["commands"]), data' "$(dirname "$0")/../tools"
    check "reference generator neutralizes availability by identifier or entirely" '[ "$code" = 0 ]'
    run reference-neutral python3 "$(dirname "$0")/../tools/generate_cli_reference.py" --build "$bin" \
        --markdown "$work/neutral.md" --json "$work/neutral.json" --neutral-availability maelys-hello
    check "reference generator accepts --neutral-availability" '[ "$code" = 0 ] && python3 -c "import json,sys; d=json.load(open(sys.argv[1]))[\"programs\"][\"maelys-hello\"]; sys.exit(0 if all(c[\"available\"] for c in d[\"commands\"]) else 1)" "$work/neutral.json"'
    check "reference generator keeps the product wording" '[ "$code" = 0 ] && grep -q "^# Référence CLI" "$work/ref.md" && grep -q "Contrat commun" "$work/ref.md" && grep -q "| Identifiant | Usage | Effet | Sortie | But |" "$work/ref.md" && grep -q "^Options globales :" "$work/ref.md" && python3 -c "import json,sys; d=json.load(open(sys.argv[1]))[\"programs\"][\"maelys-hello\"]; sys.exit(0 if \"version\" not in d and \"framework\" not in d else 1)" "$work/ref.json"'
fi

# ---- dispatcher -------------------------------------------------------------
commands="$work/commands"
mkdir -p "$commands"
cat >"$commands/hello.json" <<MANIFEST
{"schema":"maelys.cli-extension/v1","command":"hello","executable":"$hello","cliApi":1,"version":"0.1.0","summary":"Reference CLI"}
MANIFEST
export MAELYS_COMMANDS_PATH="$commands"

run d-list "$maelys" commands list --json --compact
check "dispatcher lists extensions" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"command\":\"hello\",\"executable\":\"$hello\""'

run d-help "$maelys" help
check "dispatcher help shows extension" 'printf "%s" "$out" | grep -q "hello \[ARGUMENTS...\]"'

run d-exec "$maelys" hello greet dispatcher --json --compact
check "dispatcher execs extension verbatim" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"greeting\":\"Hello, dispatcher!\""'

run d-exit "$maelys" hello run /bin/sh -c 'exit 4'
check "dispatcher propagates exit code" '[ "$code" = 4 ]'

run d-complete "$maelys" __complete -- hello no
check "dispatcher forwards completion to the extension" '[ "$code" = 0 ] && [ "$out" = "note" ]'
run d-complete-top "$maelys" __complete -- hel
check "dispatcher completes extension names" '[ "$out" = "help
hello" ]'

run d-unknown "$maelys" oci pull
check "dispatcher refuses undeclared commands" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[INVALID_COMMAND\]"'

project="$work/project"
mkdir -p "$project"
printf '# Project\n\nExisting notes.\n' >"$project/AGENTS.md"
run a-plan "$maelys" agents install "$project" --json --compact
check "agents plan writes nothing" '[ "$code" = 0 ] && [ ! -e "$project/CLAUDE.md" ] && printf "%s" "$out" | grep -q "\"mode\":\"plan\",\"changed\":false" && printf "%s" "$out" | grep -q "\"action\":\"update\""'

run a-status0 "$maelys" agents status "$project" --json --compact
check "agents status reports missing with exit 2" '[ "$code" = 2 ] && printf "%s" "$out" | grep -q "\"upToDate\":false" && printf "%s" "$out" | grep -q "\"state\":\"unmanaged\""'

run a-apply "$maelys" agents install "$project" --apply --json --compact
check "agents apply creates managed files" '[ "$code" = 0 ] && grep -q "maelys-cli:begin" "$project/AGENTS.md" && grep -q "Existing notes." "$project/AGENTS.md" && grep -q "maelys-cli:begin" "$project/CLAUDE.md" && [ -f "$project/docs/maelys-cli-guide.md" ] && [ -f "$project/.claude/skills/maelys-cli-command/SKILL.md" ]'
check "skill keeps frontmatter first" '[ "$(head -1 "$project/.claude/skills/maelys-cli-command/SKILL.md")" = "---" ]'

run a-status1 "$maelys" agents status "$project"
check "agents status current" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "current"'

run a-idempotent "$maelys" agents install "$project" --apply --json --compact
check "agents install is idempotent" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"changed\":false"'

printf '\nUser additions after the block.\n' >>"$project/CLAUDE.md"
sed -i.bak 's/maelys-cli:begin -->/maelys-cli:begin -->\nstale line/' "$project/CLAUDE.md" && rm -f "$project/CLAUDE.md.bak"
run a-status2 "$maelys" agents status "$project" --json --compact
check "agents status detects an outdated block" '[ "$code" = 2 ] && printf "%s" "$out" | grep -q "\"state\":\"outdated\""'
run a-refresh "$maelys" agents install "$project" --apply
check "agents refresh preserves user text outside the block" '[ "$code" = 0 ] && grep -q "User additions after the block." "$project/CLAUDE.md" && ! grep -q "stale line" "$project/CLAUDE.md"'

run a-codex "$maelys" agents install "$work" --client codex --json --compact
check "client filter limits files" 'printf "%s" "$out" | grep -q "AGENTS.md" && ! printf "%s" "$out" | grep -q "CLAUDE.md"'

run a-missing "$maelys" agents install "$work/absent" --json --compact
check "missing project directory" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\":\"NOT_FOUND\""'

printf '{"schema":"maelys.cli-extension/v1","command":"bad","executable":"/nonexistent/x","cliApi":1,"version":"1"}\n' >"$commands/bad.json"
run d-bad "$maelys" help
check "dispatcher refuses to start with an invalid manifest" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[ACCESS_DENIED\]"'
rm -f "$commands/bad.json"

if [ "$failures" -ne 0 ]; then
    printf '%s CLI test(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'cli-check: ok\n'
