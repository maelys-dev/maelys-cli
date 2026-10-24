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
check "help lists commands" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^  note write  Store a note in a file.$" && printf "%s" "$out" | grep -q "^AGENTS$"'
check "help gives the global options whole, and the agent contract to describe" 'printf "%s" "$out" | grep -q "Exact alias of --format json" && ! printf "%s" "$out" | grep -q "Exit 0 is success" && ! printf "%s" "$out" | grep -q "help conventions"'
run help-conventions "$hello" help conventions
check "conventions is no topic of help: it is refused like any unknown word" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[INVALID_COMMAND\]"'
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
# --help comes before what the line lacks as a whole (spec, section 8), and
# after what one option says alone.
run help-unmet-requires "$hello" limits --strict --help
check "--help answers a line whose option lacks the one it requires" '[ "$code" = 0 ] && [ -z "$err" ] && printf "%s" "$out" | grep -q "^USAGE"'
run help-unknown-option "$hello" limits --bogus --help --json --compact
check "an option the command does not have is refused before --help, in its name" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\"command\":\"limits\"" && printf "%s" "$err" | grep -q "\"code\":\"VALIDATION_FAILED\""'
# `X --help` is validated as an invocation of `help`: jsonl has no form for a
# help. The line exited 0 on an empty stdout, on a command as on a family.
run help-jsonl-direct "$hello" help greet --format jsonl --compact
direct_err=$err
check "help refuses --format jsonl" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\"code\":\"VALIDATION_FAILED\""'
run help-jsonl-option "$hello" greet --help --format jsonl --compact
check "COMMAND --help refuses --format jsonl as help does" '[ "$code" = 1 ] && [ -z "$out" ] && [ "$err" = "$direct_err" ]'
run help-jsonl-records "$hello" list --help --format jsonl --compact
check "the help of a records command is not records" '[ "$code" = 1 ] && [ -z "$out" ] && [ "$err" = "$direct_err" ]'
run help-jsonl-family "$hello" note --help --format jsonl --compact
check "FAMILY --help refuses --format jsonl as help does" '[ "$code" = 1 ] && [ -z "$out" ] && [ "$err" = "$direct_err" ]'
run help-jsonl-write "$hello" note write "$help_note" --content x --apply --help --format jsonl
check "a refused help line writes nothing either" '[ "$code" = 1 ] && [ -z "$out" ] && [ ! -e "$help_note" ]'
run help-jsonl-field "$hello" greet --help --format jsonl --field commands
check "with --field, one member of the help renders in jsonl" '[ "$code" = 0 ] && [ "$out" = "\"greet\"" ]'
run help-family-field "$hello" note --help --field commands
check "FAMILY --help honours --field" '[ "$code" = 0 ] && [ "$out" = "note.write" ]'
run help-family-unknown "$hello" note --help --bogus
check "FAMILY --help refuses an option help does not have" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "not supported by .help."'
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

run stream "$hello" run -- /bin/sh -c 'echo streamed; exit 3'
check "stream relays stdout and exit code" '[ "$code" = 3 ] && [ "$out" = "streamed" ] && [ -z "$err" ]'

# There is no short option, and a word that starts with one dash is no operand
# either (spec, section 8): before --, it is refused, in the name of the
# command. A command that takes another program's line takes it after --.
run stream-dash "$hello" run /bin/sh -c 'echo ran'
check "a word that starts with one dash is refused before --, and nothing runs" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[VALIDATION_FAILED\] Option -c is not supported: options are spelled --name"'
run dash-operand env MAELYS_CLI_FORMAT=json "$hello" greet -x --compact
check "the refusal names the command the line resolved" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\"command\":\"greet\"" && printf "%s" "$err" | grep -q "\"code\":\"VALIDATION_FAILED\""'
run dash-alone "$hello" greet -
check "a dash alone is an operand" '[ "$code" = 0 ] && [ "$out" = "Hello, -!" ]'
run dash-after "$hello" greet -- -5
check "after -- a word that starts with a dash is an operand" '[ "$code" = 0 ] && [ "$out" = "Hello, -5!" ]'
run dash-first "$hello" -h
check "-h names no command: there is no short option, --help included" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[INVALID_COMMAND\]"'
# Two options that set the same thing: the last one written wins. The same
# option twice is still a duplication.
run last-format-text "$hello" version --json --format text
check "--json then --format text renders text" '[ "$code" = 0 ] && ! printf "%s" "$out" | grep -q "schemaVersion"'
run last-format-json "$hello" version --format text --json --compact
check "--format text then --json renders JSON" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^{\"schemaVersion\":2"'
run last-layout "$hello" version --json --compact --pretty
check "--compact then --pretty indents" '[ "$code" = 0 ] && [ "$(printf "%s\n" "$out" | wc -l)" -gt 1 ]'
run twice-format "$hello" version --format json --format text
check "the same option twice is still refused" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[VALIDATION_FAILED\]"'
# A size suffix is written in capitals.
run size-capital "$hello" limits --memory 16M --json --compact
check "a size takes its suffix in capitals" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "16777216"'
run size-small "$hello" limits --memory 16m
check "a size suffix in small letters is refused" '[ "$code" = 1 ] && [ -z "$out" ] && printf "%s" "$err" | grep -q "\[VALIDATION_FAILED\]"'

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
run env-stream env MAELYS_CLI_FORMAT=json "$hello" run -- /bin/sh -c "echo plain"
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

run d-exit "$maelys" hello run -- /bin/sh -c 'exit 4'
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

# A default command (agent-cli/v2, section 8, "A line that names no command"):
# default_surface declares `show` in C, python/tests/default_surface.py the
# same catalog in Python. Each line is asked of both, and their envelopes
# must be the same bytes; then what each line must answer.
dflt_c="$bin/../tests/default_surface"
dflt_py="python3 -B $(dirname "$0")/../python/tests/default_surface.py"
# What two answers must share: the command the envelope names, the exit
# code, the error code, and the data of a success -- of a help, the commands
# it is about, its text being worded apart in C and in Python.
dflt_answer() {
    printf '%s' "${out:-$err}" | python3 -c 'import json, sys
envelope = json.load(sys.stdin)
data = envelope.get("data", {})
if envelope.get("command") == "help":
    data = data.get("commands")
print(envelope.get("command"), envelope.get("exitCode"), envelope.get("error", {}).get("code"),
      json.dumps(data, sort_keys=True, separators=(",", ":")))'
}
dflt() { # dflt NAME WORDS...: both programs, JSON by the environment
    name=$1
    shift
    run "$name" env MAELYS_CLI_FORMAT=json "$dflt_c" "$@"
    c_code=$code c_answer=$(dflt_answer)
    run "$name-py" env MAELYS_CLI_FORMAT=json $dflt_py "$@"
    answer=$(dflt_answer)
    check "default command, C and Python agree: $name" '[ "$code" = "$c_code" ] && [ "$answer" = "$c_answer" ]'
}
said() { # said PATTERN: the last answer, C and Python having agreed
    printf '%s' "$answer" | grep -q -- "$1"
}
dflt empty
check "an empty line runs the default command" '[ "$code" = 0 ] && said "^show 0 None "'
dflt option --label x
check "a line that starts with an option runs the default command" '[ "$code" = 0 ] && said "^show 0 None " && said "\"label\":\"x\""'
dflt value --label list
check "a command name an option takes as its value is that value" '[ "$code" = 0 ] && said "^show 0 None " && said "\"label\":\"list\""'
dflt bare --label x list
check "a bare word before -- is no operand of the default command" '[ "$code" = 1 ] && said "^show 1 VALIDATION_FAILED"'
dflt named list --limit x
check "a line that starts with a pattern runs that command" '[ "$code" = 1 ] && said "^list 1 VALIDATION_FAILED"'
dflt operands --label x -- notes.txt
check "the operands of the default command come after --" '[ "$code" = 0 ] && said "\"file\":\"notes.txt\""'
dflt dash -h
check "-h selects the default command and fails in its name" '[ "$code" = 1 ] && said "^show 1 VALIDATION_FAILED"'
dflt unknown nope
check "an unknown first word is never run as the default command" '[ "$code" = 1 ] && said "^unknown 1 INVALID_COMMAND"'
dflt help-default --label x --help
check "--help after an option is the help of the default command" '[ "$code" = 0 ] && said "^help 0 None \[\"show\"\]"'
dflt help-first --help
check "--help first is the general help" '[ "$code" = 0 ] && said "^help 0 None \[\"help\""'
dflt plan save --content text
check "a transaction plans without --apply" '[ "$code" = 0 ] && said "\"mode\":\"plan\""'
dflt complete-root __complete -- --
check "at the root a dash completes with the default command's options" '[ "$code" = 0 ] && said "\"word\":\"--label\""'
dflt complete-after __complete -- --level ""
check "after a first option the default command's values complete" '[ "$code" = 0 ] && said "\"word\":\"high\""'
for form in "describe" "describe --summary" "describe --summary --prefix list" "describe list"; do
    run "describe-default-$form" "$dflt_c" $form --json --compact
    check "every form of describe names the default command: $form" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "\"defaultCommand\":\"show\""'
done
# The general help says it in one sentence: which command, both ways to
# invoke it, and that the options in front are its (section 8); where its
# operands go is in its own help, with both forms on lines of their own.
run help-default "$dflt_c" help
check "help names the default command and shows both forms" '[ "$code" = 0 ] && printf "%s" "$out" | tr "\n" " " | grep -q "default-surface \[options\] runs .show., as *default-surface show \[options\] does"'
run help-default-own "$dflt_c" show --help
check "the default command's help shows both forms, operands after --" '[ "$code" = 0 ] && printf "%s" "$out" | grep -q "^  default-surface \[options\] \[-- OPERANDS\]$" && printf "%s" "$out" | grep -q "^  default-surface show \[options\] \[-- OPERANDS\]$"'
run help-no-default "$hello" help
check "a program without a default command says nothing of one" '[ "$code" = 0 ] && ! printf "%s" "$out" | grep -q "DEFAULT COMMAND"'
run help-says-options "$dflt_c" help
check "help says a line that starts with an option is the default command's" 'printf "%s" "$out" | tr "\n" " " | grep -q "the options in front, other than --help and --version, are its"'
run help-says-options-py $dflt_py show --help
check "the default command's help says it too" 'printf "%s" "$out" | tr "\n" " " | grep -q "starts *with an option other than --help *and --version"'

# Migration (section 8: declaring a default command can stop a line that
# ran): the same catalog before a default is declared
# (DEFAULT_SURFACE_DEFAULT=none) and after, in C and in Python. Each handler
# appends its command to a trace: a handler ran exactly when a command of
# the catalog succeeded, never on a refusal, --help or --version. Without a
# default C and Python read an empty line and a line that starts with an
# option apart; the specification leaves that open, and these lines say
# what each does today.
mig_trace="$work/default-trace"
mig_answer() { # the command, the exit code and the error code; a stream's first line
    printf '%s' "${out:-$err}" | python3 -c 'import json, sys
text = sys.stdin.read()
try:
    envelope = json.loads(text)
except ValueError:
    print("stream", sys.argv[1], (text.splitlines() or [""])[0])
    sys.exit()
print(envelope.get("command"), envelope.get("exitCode"), envelope.get("error", {}).get("code"))' "$code"
}
mig_one() { # mig_one NAME DEFAULT PROGRAM WORDS...: $answer, the handler checked
    mig_name=$1 mig_default=$2 mig_program=$3
    shift 3
    : >"$mig_trace"
    run "$mig_name" env DEFAULT_SURFACE_DEFAULT="$mig_default" DEFAULT_SURFACE_TRACE="$mig_trace" \
        MAELYS_CLI_FORMAT=json $mig_program "$@"
    answer=$(mig_answer)
    ran=$(cat "$mig_trace")
    case "$answer" in
        "stream 0 "*) expected=$mig_default ;;
        help\ *|version\ *|*" 1 "*|*" 2 "*) expected= ;;
        *) expected=${answer%% *} ;;
    esac
    check "a handler runs only for a command that succeeds: $mig_name ($answer)" '[ "$ran" = "$expected" ]'
}
mig() { # mig NAME DEFAULT BEFORE_C BEFORE_PY AFTER WORDS...
    m_name=$1 m_default=$2 m_before_c=$3 m_before_py=$4 m_after=$5
    shift 5
    mig_one "$m_name-before" none "$dflt_c" "$@"
    check "before a default, C: $m_name" '[ "$answer" = "$m_before_c" ]'
    mig_one "$m_name-before-py" none "$dflt_py" "$@"
    check "before a default, Python: $m_name" '[ "$answer" = "$m_before_py" ]'
    mig_one "$m_name-after" "$m_default" "$dflt_c" "$@"
    check "after a default, C: $m_name" '[ "$answer" = "$m_after" ]'
    mig_one "$m_name-after-py" "$m_default" "$dflt_py" "$@"
    check "after a default, Python: $m_name" '[ "$answer" = "$m_after" ]'
}
mig empty show "help 0 None" "unknown 1 INVALID_COMMAND" "show 0 None"
mig json-first show "unknown 1 INVALID_COMMAND" "list 0 None" "show 1 VALIDATION_FAILED" --json list
mig json-after show "list 0 None" "list 0 None" "list 0 None" list --json
mig value-list show "unknown 1 INVALID_COMMAND" "list 1 VALIDATION_FAILED" "show 0 None" --label list
mig unknown show "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" nope
mig help show "help 0 None" "help 0 None" "help 0 None" --help
mig version show "version 0 None" "version 0 None" "version 0 None" --version
mig help-implicit show "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "help 0 None" --label x --help
mig dash-h show "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "show 1 VALIDATION_FAILED" -h
mig dash-x show "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "show 1 VALIDATION_FAILED" -x
mig operands show "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "show 0 None" --label x -- notes.txt
mig stream-json watch "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "watch 1 VALIDATION_FAILED" --json
mig stream-empty watch "help 0 None" "unknown 1 INVALID_COMMAND" "stream 0 watching"
mig transaction save "unknown 1 INVALID_COMMAND" "unknown 1 INVALID_COMMAND" "save 0 None" --content text
check "a default transaction without --apply plans" 'printf "%s" "$out" | grep -q "\"mode\": *\"plan\""'

# The help palette (maelys_cli_help_palette_t; help_palette= in Python):
# default_surface declares one for every role. C and Python print the same
# colored bytes where color is forced, the plain help once the colors are
# taken out, and nothing colored under --color never or in data.text.
for words in "help" "show --help" "list --help"; do
    run "palette-$words" env CLICOLOR_FORCE=1 "$dflt_c" $words
    c_colored=$out
    run "palette-$words-py" env CLICOLOR_FORCE=1 $dflt_py $words
    check "C and Python color the help alike: $words" '[ "$out" = "$c_colored" ] && printf "%s" "$out" | grep -q "$(printf "\033")\[33m--"'
    run "palette-$words-plain" "$dflt_c" $words
    check "the colored help is the plain one, colors taken out: $words" '[ "$(printf "%s" "$c_colored" | sed "s/$(printf "\033")\[[0-9;]*m//g")" = "$out" ]'
done
run palette-never env CLICOLOR_FORCE=1 "$dflt_c" help --color never
check "--color never shows no color, CLICOLOR_FORCE notwithstanding" '! printf "%s" "$out" | grep -q "$(printf "\033")"'
run palette-json env CLICOLOR_FORCE=1 "$dflt_c" help --json --compact
check "data.text is never colored" '! printf "%s" "$out" | grep -q "u001b"'

# The language of human text: default_surface declares French for part of
# its catalog, in C and in Python alike, and maelys-hello declares none.
# The first of LC_ALL, LC_MESSAGES and LANG that is not empty decides; a
# language the program does not have, C or POSIX leaves it in English; the
# texts left untranslated stay English; JSON and describe never change.
lang_of() { # lang_of ENV...: the usage heading of default_surface's help
    run "lang" env -u LC_ALL -u LC_MESSAGES -u LANG -u LANGUAGE -u MAELYS_CLI_LANG "$@" "$dflt_c" help
    printf '%s\n' "$out" | grep -x -e USAGE -e UTILISATION | head -n 1
}
check "LANG=fr_FR.UTF-8 shows the help in French" '[ "$(lang_of LANG=fr_FR.UTF-8)" = UTILISATION ]'
check "LANG=en_US.UTF-8 shows it in English" '[ "$(lang_of LANG=en_US.UTF-8)" = USAGE ]'
check "LANG=C shows it in English" '[ "$(lang_of LANG=C)" = USAGE ]'
check "LANG=C.UTF-8 shows it in English" '[ "$(lang_of LANG=C.UTF-8)" = USAGE ]'
check "no locale at all shows it in English" '[ "$(lang_of)" = USAGE ]'
check "LC_ALL comes before LANG" '[ "$(lang_of LC_ALL=C LANG=fr_FR.UTF-8)" = USAGE ] && [ "$(lang_of LC_ALL=fr_FR.UTF-8 LANG=C)" = UTILISATION ]'
check "LC_MESSAGES comes before LANG" '[ "$(lang_of LC_MESSAGES=fr_FR.UTF-8 LANG=en_US.UTF-8)" = UTILISATION ]'
check "an empty LC_ALL is no source" '[ "$(lang_of LC_ALL= LANG=fr_FR.UTF-8)" = UTILISATION ]'
check "fr_CA falls back to fr" '[ "$(lang_of LANG=fr_CA.UTF-8)" = UTILISATION ]'
check "a language the program does not have is English, later sources unread" '[ "$(lang_of LC_ALL=de_DE.UTF-8 LANG=fr_FR.UTF-8)" = USAGE ]'
check "LANGUAGE is not read" '[ "$(lang_of LANGUAGE=fr LANG=C)" = USAGE ]'
check "MAELYS_CLI_LANG comes before the locale" '[ "$(lang_of MAELYS_CLI_LANG=fr LC_ALL=C)" = UTILISATION ] && [ "$(lang_of MAELYS_CLI_LANG=en LANG=fr_FR.UTF-8)" = USAGE ]'
run lang-option env MAELYS_CLI_LANG=en "$dflt_c" help --lang=fr
check "--lang comes before MAELYS_CLI_LANG" 'printf "%s" "$out" | grep -qx UTILISATION'
check "a malformed MAELYS_CLI_LANG is ignored, the canonical language kept" '[ "$(lang_of MAELYS_CLI_LANG=fr_FR LANG=fr_FR.UTF-8)" = USAGE ]'
run lang-zz env LC_ALL=fr_FR.UTF-8 "$dflt_c" help --lang zz
zz=$out
run lang-c env LC_ALL=C "$dflt_c" help
check "--lang zz under a French locale gives the canonical text" '[ "$zz" = "$out" ]'
run lang-upper env LC_ALL=C "$dflt_c" help --lang FR
upper=$out
run lang-lower env LC_ALL=C "$dflt_c" help --lang fr
check "--lang FR is --lang fr" '[ "$upper" = "$out" ] && printf "%s" "$out" | grep -q UTILISATION'
for tag in en_US e english; do
    run "lang-bad-$tag" "$dflt_c" version --lang "$tag" --json
    check "--lang $tag is refused" '[ "$code" = 1 ] && printf "%s" "$err" | grep -q VALIDATION_FAILED'
done
run lang-help-json env LC_ALL=fr_FR.UTF-8 "$dflt_c" help --json
french=$(printf '%s' "$out" | python3 -c 'import json, sys; sys.stdout.write(json.load(sys.stdin)["data"]["text"])')
run lang-help-text env LC_ALL=fr_FR.UTF-8 "$dflt_c" help
check "help --json carries the text help shows" '[ "$french" = "$out" ]'
for words in "help" "show --help" "list --help" "save --help" "watch --help" "help list" "show --label !x"; do
    run "lang-$words" env LC_ALL=fr_FR.UTF-8 "$dflt_c" $words
    c_french="$out$err"
    run "lang-$words-py" env LC_ALL=fr_FR.UTF-8 $dflt_py $words
    check "C and Python write the same French: $words" '[ "$out$err" = "$c_french" ]'
    run "lang-$words-en" env LC_ALL=C "$dflt_c" $words
    check "the French is another text than the English: $words" '[ "$out$err" != "$c_french" ]'
done
run lang-failure env LC_ALL=fr_FR.UTF-8 "$dflt_c" show --label '!x'
check "a product's failure template is translated, its argument put back, Hint: kept" \
    'printf "%s" "$err" | grep -q "^default-surface: \[PRECONDITION_FAILED\] L.étiquette !x est réservée\.$" && printf "%s" "$err" | grep -q "^Conseil : Choisissez"'
run lang-framework env LC_ALL=fr_FR.UTF-8 "$dflt_c" list --limit x
check "a framework failure is translated" 'printf "%s" "$err" | grep -q "^default-surface: \[VALIDATION_FAILED\] L.option --limit attend"'
run lang-framework-py env LC_ALL=fr_FR.UTF-8 $dflt_py list --limit x
check "a Python framework failure is translated" 'printf "%s" "$err" | grep -q "^default-surface: \[VALIDATION_FAILED\] L.option --limit attend"'
run lang-partial env LC_ALL=fr_FR.UTF-8 "$dflt_c" help
check "a text the table leaves out stays in English" 'printf "%s" "$out" | grep -q "Watch the entries on stdout\."'
for program in "$dflt_c" "$dflt_py"; do
    for words in "help --field commands" "help show --field commands" "describe --format json" \
            "describe --summary --format json" "version --json" "show --label !x --json" "list --limit x --json" \
            "nope --json" "show --json" "__complete --json -- --"; do
        run "lang-json-fr" env LC_ALL=fr_FR.UTF-8 $program $words
        french="$code|$out|$err"
        run "lang-json-c" env LC_ALL=C $program $words
        check "a machine form is the same bytes in every language: $program $words" '[ "$code|$out|$err" = "$french" ]'
    done
done
for words in "--help" "help greet" "greet --times 99"; do
    run "lang-hello-fr" env LC_ALL=fr_FR.UTF-8 "$hello" $words
    french="$out$err"
    run "lang-hello-c" env LC_ALL=C "$hello" $words
    check "a product without a table stays in English: $words" '[ "$out$err" = "$french" ]'
done

# The built-ins are declared alike in C and in Python: purposes, operands,
# options, schemas, and the --apply every transaction takes. describe of a C
# product and of a Python one say the same of them.
builtins_of() { # builtins_of COMMAND...: the built-in descriptors and --apply, as one JSON
    "$@" describe --json --compact | python3 -c 'import json, sys
commands = json.load(sys.stdin)["data"]["commands"]
kept = {c["id"]: c for c in commands if c["id"] in ("help", "version", "describe", "completion", "complete.candidates")}
kept["--apply"] = next(o for c in commands for o in c["input"]["options"] if o["long"] == "--apply")
print(json.dumps(kept, sort_keys=True))'
}
c_builtins=$(builtins_of "$dflt_c")
py_builtins=$(builtins_of $dflt_py)
check "C and Python declare the built-ins and --apply alike" '[ -n "$c_builtins" ] && [ "$c_builtins" = "$py_builtins" ]'

if [ "$failures" -ne 0 ]; then
    printf '%s CLI test(s) failed\n' "$failures" >&2
    exit 1
fi
printf 'cli-check: ok\n'
