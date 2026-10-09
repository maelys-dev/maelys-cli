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
check "describe usage equals synopsis" 'printf "%s" "$out" | grep -q "\"usage\":\"note write FILE --content TEXT \[--replace\] \[--apply\] \[--expect FINGERPRINT\]\",.*\"synopsis\":\"note write FILE --content TEXT \[--replace\] \[--apply\] \[--expect FINGERPRINT\]\""'

run help "$hello" help
check "help lists commands" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^  note write  Store a note in a file.$" && printf "%s" "$out" | grep -q "AGENT CONTRACT"'
check "help names what every program has in common, and says where it is spelled out" 'printf "%s" "$out" | grep -q "help conventions" && ! printf "%s" "$out" | grep -q "Exact alias of --format json"'
run help-conventions "$hello" help conventions
check "help conventions has the global options and the agent contract" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "Exact alias of --format json" && printf "%s" "$out" | grep -q "Exit 0 is success"'
run help "$hello" help
check "help fits eighty columns" '[ "$(printf "%s\n" "$out" | awk "{ if (length(\$0) > m) m = length(\$0) } END { print m }")" -le 80 ]'
run help-family "$hello" note --help
check "the help of a family lists its commands with their usage" '[ "$code" = 0 ] && [ -z "$err" ] && printf "%s" "$out" | grep -q "^  note write FILE --content TEXT" && printf "%s" "$out" | grep -q "^      Store a note in a file.$"'
family_help=$out
run help-family-id "$hello" help note
check "help FAMILY and FAMILY --help say the same" '[ "$code" = 0 ] && [ "$out" = "$family_help" ]'
run help-option-json "$hello" greet --help --format json --compact
check "COMMAND --help answers in the envelope of help, naming the command in data" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"command\":\"help\",\"ok\":true" && printf "%s" "$out" | grep -q "\"commands\":\[\"greet\"\]"'
help_note="$work/help-note.txt"
run help-never-runs "$hello" note write "$help_note" --content x --apply --help
check "nothing runs under --help, --apply included" '[ "$code" = 0 ] && [ ! -e "$help_note" ] && printf "%s" "$out" | grep -q "^USAGE"'
run help-examples "$hello" help note.write
check "the help of a command shows its examples, each a line to copy" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^EXAMPLES$" && printf "%s" "$out" | grep -q "^  maelys-hello note write /tmp/note.txt --content hello --apply$" && printf "%s" "$out" | grep -q "^      Write it.$"'
# A hidden command is never offered, and answered when named (spec 2.13.1,
# section 6): the general help does not list it, its own help names it.
run help-general "$hello" help --json --compact
check "the general help lists no hidden command" '[ "$code" = 0 ] && ! printf "%s" "$out" | grep -q "complete.candidates"'
run help-hidden "$hello" help complete.candidates --json --compact
check "help asked of a hidden command names it" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"command\":\"help\"" && printf "%s" "$out" | grep -q "\"commands\":\[\"complete.candidates\"\]"'
run help-hidden-flag "$hello" __complete --help --json --compact
check "--help after a hidden command names it too" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"commands\":\[\"complete.candidates\"\]"'
run describe-examples "$hello" describe note.write --format json --compact
check "describe COMMAND_ID carries them, one word per element" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"examples\":\[{\"words\":\[\"note\",\"write\",\"/tmp/note.txt\",\"--content\",\"hello\"\],\"summary\":\"Plan the note: nothing is written.\"}"'
run describe-summary-examples "$hello" describe --summary --format json --compact
check "the summary omits them" '[ "$code" = 0 ] && ! printf "%s" "$out" | grep -q "\"examples\""'
run help-not-family "$hello" note
check "words that name no command are an error without --help" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[INVALID_COMMAND\]"'

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

run records-text "$hello" list --limit 2
check "text records into a pipe are tab-separated rows" '[ "$code" = 0 ] && [ "$out" = "0	alpha
1	beta" ]'
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
run complete-free "$hello" __complete -- greet ""
check "a free-text operand completes to nothing, so the shell offers files" '[ "$code" = 0 ] && [ -z "$out" ]'
# After a delegate's pattern the words are the delegate's own, the same in
# every format; a word carrying a control byte is dropped, not relayed.
printf '#!/bin/sh\n[ "$1" = __complete ] || exit 9\nshift\nprintf "%%s\\n" "inspect" "" "list" "$*"\nprintf "bad\\033[2J\\n"\n' >"$helper"
chmod 0755 "$helper"
run delegate-complete "$hello" __complete -- image in
check "completion after a delegate is the delegate's, asked in text" '[ "$code" = 0 ] && [ "$out" = "inspect
list
--format text -- in" ]'
run delegate-complete-json "$hello" __complete --format json --compact -- image in
check "and the same words in JSON" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"count\":3,\"records\":\[{\"word\":\"inspect\"},{\"word\":\"list\"},{\"word\":\"--format text -- in\"}\]"'
rm -f "$helper"
run delegate-complete-missing "$hello" __complete -- image in
check "none, silently, when the delegate is not installed" '[ "$code" = 0 ] && [ -z "$out" ] && [ -z "$err" ]'
run complete-option "$hello" __complete -- limits --level ""
check "completion of choices" '[ "$out" = "low
high" ]'
run trunk-json "$hello" version --verbose --progress always --pager always --json --compact
check "trunk diagnostics are silent in JSON" '[ "$code" = 0 ] && [ -z "$err" ] && printf "%s" "$out" | grep -q "\"ok\":true"'
run trunk-text "$hello" greet Ada --verbose --progress always
check "verbose details go to stderr with the program prefix, stdout unchanged" '[ "$code" = 0 ] && [ "$out" = "Hello, Ada!" ] && printf "%s" "$err" | grep -q "^maelys-hello: greeting Ada 1 time" && ! printf "%s" "$err" | grep -q "maelys-hello: \["'
run trunk-quiet "$hello" greet Ada --verbose=false --progress=never --pager=never
check "false and never trunk values are silent" '[ "$code" = 0 ] && [ "$out" = "Hello, Ada!" ] && [ -z "$err" ]'
marker=$(mktemp -u)
run pager-pipe env PAGER="sh -c 'touch $marker; cat'" "$hello" version --pager always
check "no pager starts in a pipe, whatever --pager says" '[ "$code" = 0 ] && [ "$out" = "maelys-hello 0.1.0" ] && [ ! -e "$marker" ]'
rm -f "$marker"
run pattern-refused "$hello" describe --summary --prefix "Bad." --json
check "a declared pattern is enforced" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "expects a value matching"'
run field-scalar "$hello" describe --field program
check "--field renders one scalar member" '[ "$code" = 0 ] && [ "$out" = "maelys-hello" ]'
run field-records "$hello" list --limit 2 --field records
check "--field records on a json-records command equals its own rendering" '[ "$code" = 0 ] && [ "$out" = "0	alpha
1	beta" ]'
run field-json-refused "$hello" describe --field program --json
check "--field with --format json is refused" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "conflicts with --format json"'
run field-absent "$hello" describe --field no-such-member
check "--field of a member data does not carry fails once the command has run" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "names .no-such-member."'
run hidden-describe "$hello" describe greet --json
check "hidden option listed by describe" 'printf "%s" "$out" | grep -q "\"long\": \"--trace\"" && printf "%s" "$out" | grep -q "\"hidden\": true" && ! printf "%s" "$out" | grep -q "trace\]"'
run hidden-help "$hello" help greet
check "hidden option absent from help and usage" '[ "$code" = 0 ] && ! printf "%s" "$out" | grep -q -- "--trace"'
run hidden-complete "$hello" __complete -- greet --
check "hidden option never completed" 'printf "%s" "$out" | grep -q -- "--shout" && ! printf "%s" "$out" | grep -q -- "--trace"'
run hidden-accepted "$hello" greet x --trace --json
check "hidden option accepted and traced on stderr" '[ "$code" = 0 ] && printf "%s" "$err" | grep -q "warning: greet: name=x"'
# --expect binds --apply to the reviewed plan (spec 2.9, section 4).
bound="$work/bound.txt"
run plan-fingerprint "$hello" note write "$bound" --content first --field fingerprint
reviewed=$out
check "a plan carries its fingerprint" '[ "$code" = 0 ] && printf "%s" "$reviewed" | grep -Eq "^sha256:[0-9a-f]{64}$"'
run plan-again "$hello" note write "$bound" --content first --field fingerprint
check "the same plan over the same state has the same fingerprint" '[ "$out" = "$reviewed" ]'
run plan-other "$hello" note write "$bound" --content second --field fingerprint
check "another action has another" '[ "$code" = 0 ] && [ "$out" != "$reviewed" ]'
run expect-stale "$hello" note write "$bound" --content second --apply --expect "$reviewed"
check "an action that is not the reviewed one is refused before anything is written" '[ "$code" = 1 ] && [ ! -e "$bound" ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[PRECONDITION_FAILED\]"'
printf 'someone else' >"$bound"
run expect-moved "$hello" note write "$bound" --content first --replace --apply --expect "$reviewed"
check "the same action over a state that moved is refused too" '[ "$code" = 1 ] && [ "$(cat "$bound")" = "someone else" ] && printf "%s" "$err" | grep -q "Plan again without --apply"'
rm -f "$bound"
run expect-apply "$hello" note write "$bound" --content first --apply --expect "$reviewed" --field fingerprint
check "the reviewed plan is applied, and the result carries its fingerprint" '[ "$code" = 0 ] && [ "$(cat "$bound")" = "first" ] && [ "$out" = "$reviewed" ]'
run expect-plan "$hello" note write "$work/unbound.txt" --content x --expect "$reviewed"
check "--expect without --apply is refused" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "requires --apply"'

note_field="$work/field-refused.txt"
run field-write "$hello" note write "$note_field" --content hi --apply --field no-such-member
check "--field of a member a transaction does not require is refused before anything is written" '[ "$code" = 1 ] && [ ! -e "$note_field" ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "does not always return"'
run field-write-ok "$hello" note write "$note_field" --content hi --apply --field path
check "--field of a member it requires runs the transaction" '[ "$code" = 0 ] && [ -e "$note_field" ] && [ "$out" = "$note_field" ]'
note_env="$work/env-field.txt"
run env-field env MAELYS_CLI_FORMAT=json "$hello" note write "$note_env" --content hi --apply --field path
check "--field against the environment's json is refused before anything is written" '[ "$code" = 1 ] && [ ! -e "$note_env" ] && printf "%s" "$err" | grep -q "conflicts with --format json"'
run env-compact env MAELYS_CLI_FORMAT=json "$hello" greet Ada --compact
check "--compact leaves the environment's format in force" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^{\"schemaVersion\":2,.*\"greeting\":\"Hello, Ada!\""'
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
check "the dispatcher's help fits eighty columns, its own guidance included" '[ "$(printf "%s\n" "$out" | awk "{ if (length(\$0) > m) m = length(\$0) } END { print m }")" -le 80 ] && printf "%s" "$out" | grep -q "^EXTERNAL COMMANDS$"'
check "dispatcher help shows extension" 'printf "%s" "$out" | grep -Eq "^  hello +Reference CLI"'
run d-help-extension "$maelys" help hello
check "and the extension's own help gives its usage" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "hello \[ARGUMENTS...\]"'

# A manifest a package manager linked into its prefix: the link is followed
# and the file it resolves to is judged, in the directory that holds it. This
# is how every Homebrew-installed extension is discovered.
cellar="$work/cellar/share/maelys/commands"
mkdir -p "$cellar"
cp "$commands/hello.json" "$cellar/linked.json"
sed -i.bak 's/"command":"hello"/"command":"linked"/' "$cellar/linked.json"
rm -f "$cellar/linked.json.bak"
ln -s "$cellar/linked.json" "$commands/linked.json"
run d-linked "$maelys" commands list --json --compact
check "dispatcher follows a symlinked manifest" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"command\":\"linked\",\"executable\":\"$hello\"" && printf "%s" "$out" | grep -q "\"manifest\":\"$commands/linked.json\""'

# The directory the link resolves to is what says who may replace the file.
chmod 0777 "$cellar"
run d-linked-open "$maelys" commands list
check "dispatcher refuses a manifest in a world-writable directory" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[ACCESS_DENIED\]" && printf "%s" "$err" | grep -q "directory"'
chmod 0755 "$cellar"
run d-linked-again "$maelys" commands list --json --compact
check "dispatcher accepts it again once the directory is closed" '[ "$code" = 0 ]'
rm -f "$commands/linked.json"

# One extension this machine cannot run does not cost the dispatcher: the
# command is declared, described unavailable with the code that names the
# cause, and every built-in keeps working. A digest that does not match is a
# refusal of trust (ACCESS_DENIED), not an absence (UNSUPPORTED).
cat >"$commands/stale.json" <<MANIFEST
{"schema":"maelys.cli-extension/v1","command":"stale","executable":"$hello","cliApi":1,"version":"0.1.0","summary":"Stale digest","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}
MANIFEST
run d-stale-version "$maelys" --version
check "a stale extension does not stop the dispatcher" '[ "$code" = 0 ] && [ -z "$err" ]'
run d-stale-list "$maelys" commands list --json --compact
check "an unusable extension is listed as unavailable" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"command\":\"stale\"" && printf "%s" "$out" | grep -q "\"available\":false" && printf "%s" "$out" | grep -q "\"unavailableCode\":\"ACCESS_DENIED\""'
run d-stale-describe "$maelys" describe stale --json --compact
check "describe names it unavailable with its reason" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"available\":false" && printf "%s" "$out" | grep -q "does not match the sha256"'
run d-stale-run "$maelys" stale --format json --compact
check "invoking it answers the code of its cause" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\":\"ACCESS_DENIED\""'
run d-stale-complete "$maelys" __complete -- st
check "completion never offers an unavailable command" '[ "$code" = 0 ] && ! printf "%s\n" "$out" | grep -qx "stale"'
rm -f "$commands/stale.json"

# A manifest that cannot be trusted or understood still stops everything: no
# catalog can be built from it, and ignoring it would be too easy to miss.
cat >"$commands/unsafe.json" <<MANIFEST
{"schema":"maelys.cli-extension/v1","command":"unsafe","executable":"$hello","cliApi":1,"version":"1.0.0","summary":"clear\\u001b[2J"}
MANIFEST
run d-unsafe "$maelys" help
check "dispatcher refuses terminal controls in extension metadata" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[PROTOCOL_FAILED\]" && ! printf "%s" "$err" | grep -q "$(printf "\033")"'
rm -f "$commands/unsafe.json"

cat >"$commands/broken.json" <<MANIFEST
{"schema":"maelys.cli-extension/v1","version":}
MANIFEST
run d-broken "$maelys" help
check "an unparsable manifest names the failing member" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "not valid JSON at /version:"'
rm -f "$commands/broken.json"

run d-exec "$maelys" hello greet dispatcher --json --compact
check "dispatcher execs extension verbatim" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"greeting\":\"Hello, dispatcher!\""'

run d-exit "$maelys" hello run /bin/sh -c 'exit 4'
check "dispatcher propagates exit code" '[ "$code" = 4 ]'

run d-complete "$maelys" __complete -- hello no
check "dispatcher forwards completion to the extension" '[ "$code" = 0 ] && [ "$out" = "note" ]'
run d-complete-json "$maelys" __complete --format json --compact -- hello no
check "in JSON as in text" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"records\":\[{\"word\":\"note\"}\]"'
run d-complete-once "$maelys" __complete -- ag
check "a word two commands start with is offered once" '[ "$code" = 0 ] && [ "$out" = "agents" ]'
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

# The plan of an installation is bound to its application (spec 2.9, section
# 4): the fingerprint covers the project, the clients and, for each file, what
# is there and what would be written.
reviewed=$("$maelys" agents install "$project" --field fingerprint)
check "agents plan carries a fingerprint, the same for the same plan" 'printf "%s" "$reviewed" | grep -Eq "^sha256:[0-9a-f]{64}$" && [ "$("$maelys" agents install "$project" --field fingerprint)" = "$reviewed" ]'
check "another client is another plan" '[ "$("$maelys" agents install "$project" --client codex --field fingerprint)" != "$reviewed" ]'
run a-plan-text "$maelys" agents install "$project"
check "the text of a plan gives the option that binds it" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^  --expect $reviewed$"'
run a-expect-plan "$maelys" agents install "$project" --expect "$reviewed"
check "agents install refuses --expect without --apply" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "requires --apply" && [ ! -e "$project/CLAUDE.md" ]'
cp "$project/AGENTS.md" "$work/AGENTS.reviewed"
printf 'Edited after the plan was read.\n' >>"$project/AGENTS.md"
cp "$project/AGENTS.md" "$work/AGENTS.edited"
run a-expect-stale "$maelys" agents install "$project" --apply --expect "$reviewed" --json --compact
check "a file edited since the plan refuses the application, before any write" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\"code\":\"PRECONDITION_FAILED\"" && [ ! -e "$project/CLAUDE.md" ] && [ ! -e "$project/docs" ] && cmp -s "$project/AGENTS.md" "$work/AGENTS.edited"'
cp "$work/AGENTS.reviewed" "$project/AGENTS.md"
check "the same files again are the reviewed plan again" '[ "$("$maelys" agents install "$project" --field fingerprint)" = "$reviewed" ]'

run a-status0 "$maelys" agents status "$project" --json --compact
check "agents status reports missing with exit 2" '[ "$code" = 2 ] && printf "%s" "$out" | grep -q "\"upToDate\":false" && printf "%s" "$out" | grep -q "\"state\":\"unmanaged\""'

run a-apply "$maelys" agents install "$project" --apply --expect "$reviewed" --json --compact
check "the reviewed plan applies, and answers its fingerprint" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"mode\":\"apply\",\"changed\":true" && printf "%s" "$out" | grep -q "\"fingerprint\":\"$reviewed\""'
check "agents apply creates managed files" '[ "$code" = 0 ] && grep -q "maelys-cli:begin" "$project/AGENTS.md" && grep -q "Existing notes." "$project/AGENTS.md" && grep -q "maelys-cli:begin" "$project/CLAUDE.md" && [ -f "$project/docs/maelys-cli-guide.md" ] && [ -f "$project/.claude/skills/maelys-cli-command/SKILL.md" ]'
check "skill keeps frontmatter first" '[ "$(head -1 "$project/.claude/skills/maelys-cli-command/SKILL.md")" = "---" ]'
check "generated texts stamp a commit next to the version, not a date" \
    'grep -Eq "maelys-cli [0-9]+\.[0-9]+\.[0-9]+ \(([0-9a-f]{7}|unknown)\)" "$project/docs/maelys-cli-guide.md" && \
     grep -Eq "maelys-cli [0-9]+\.[0-9]+\.[0-9]+ \(([0-9a-f]{7}|unknown)\)" "$project/.claude/skills/maelys-cli-command/SKILL.md" && \
     grep -Eq "maelys-cli [0-9]+\.[0-9]+\.[0-9]+, ([0-9a-f]{7}|unknown)\)" "$project/AGENTS.md" && \
     grep -Eq "maelys-cli [0-9]+\.[0-9]+\.[0-9]+, ([0-9a-f]{7}|unknown)\)" "$project/CLAUDE.md"'
for agent_file in AGENTS.md CLAUDE.md docs/maelys-cli-guide.md .claude/skills/maelys-cli-command/SKILL.md; do
    check "agent attribution survives installation: $agent_file" 'grep -q "SPDX-License-Identifier: CC-BY-4.0" "$project/$agent_file" && grep -q "Copyright 2026 David Bromberg" "$project/$agent_file" && grep -q "https://creativecommons.org/licenses/by/4.0/" "$project/$agent_file" && grep -q "Source: https://github.com/maelys-dev/maelys-cli/" "$project/$agent_file" && ! grep -q "CC0" "$project/$agent_file"'
done

run a-status1 "$maelys" agents status "$project"
check "agents status current" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "current"'

run a-idempotent "$maelys" agents install "$project" --apply --json --compact
check "agents install is idempotent" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"changed\":false"'
run a-expect-applied "$maelys" agents install "$project" --apply --expect "$reviewed" --json --compact
check "a plan already applied is no longer the plan" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\":\"PRECONDITION_FAILED\""'

printf '\nUser additions after the block.\n' >>"$project/CLAUDE.md"
sed -i.bak 's/maelys-cli:begin -->/maelys-cli:begin -->\nstale line/' "$project/CLAUDE.md" && rm -f "$project/CLAUDE.md.bak"
run a-status2 "$maelys" agents status "$project" --json --compact
check "agents status detects an outdated block" '[ "$code" = 2 ] && printf "%s" "$out" | grep -q "\"state\":\"outdated\""'
run a-refresh "$maelys" agents install "$project" --apply
check "agents refresh preserves user text outside the block" '[ "$code" = 0 ] && grep -q "User additions after the block." "$project/CLAUDE.md" && ! grep -q "stale line" "$project/CLAUDE.md"'

run a-codex "$maelys" agents install "$work" --client codex --json --compact
check "client filter limits files" 'printf "%s" "$out" | grep -q "AGENTS.md" && ! printf "%s" "$out" | grep -q "CLAUDE.md"'

symlink_project="$work/symlink-project"
outside_project="$work/outside-project"
mkdir -p "$symlink_project" "$outside_project"
ln -s "$outside_project" "$symlink_project/docs"
run a-symlink "$maelys" agents install "$symlink_project" --client codex --apply --json --compact
check "agents install refuses symbolic-link parents" '[ "$code" = 1 ] && [ ! -e "$outside_project/maelys-cli-guide.md" ]'

run a-missing "$maelys" agents install "$work/absent" --json --compact
check "missing project directory" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\":\"NOT_FOUND\""'

# An executable that is gone is an unavailable command, not a dead dispatcher,
# and NOT_FOUND rather than a refusal of trust: an agent retries an install on
# the first and never on the second.
printf '{"schema":"maelys.cli-extension/v1","command":"bad","executable":"/nonexistent/x","cliApi":1,"version":"1"}\n' >"$commands/bad.json"
run d-bad "$maelys" help
check "a manifest whose executable is gone leaves the dispatcher running" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "bad"'
run d-bad-run "$maelys" bad --format json --compact
check "and its command answers NOT_FOUND" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\"code\":\"NOT_FOUND\""'
rm -f "$commands/bad.json"

# A manifest nothing can trust still stops everything: no catalog can be built
# from it, and skipping it with a warning would be too easy to miss.
printf '{"schema":"maelys.cli-extension/v1","command":"untrusted","executable":"%s","cliApi":1,"version":"1"}\n' "$hello" >"$commands/untrusted.json"
chmod 0666 "$commands/untrusted.json"
run d-untrusted "$maelys" help
check "an untrusted manifest stops the dispatcher" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q "\[ACCESS_DENIED\]"'
rm -f "$commands/untrusted.json" 
rm -f "$commands/bad.json"

if [ "$failures" -ne 0 ]; then
    printf '%s CLI test(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'cli-check: ok\n'
