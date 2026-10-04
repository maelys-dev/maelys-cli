#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The completion scripts offer, in their own shell, the words `__complete` returns.

Usage: completion-check.py C_HELLO PYTHON_HELLO

Drives `completion bash|zsh|fish` of both reference products in every shell
that is installed, and compares what the script offers with the oracle,
`PROGRAM __complete -- WORDS...` (agent-cli/v2, section 6): the same words
for every word list, and the shell's file completion when the oracle returns
none. The conformance kit of agent-cli-spec proves the same on the shell the
PATH names; this adds what it does not drive:

  bash   every bash found, /bin/bash included: macOS ships 3.2 there, which
         joins "${array[@]:offset:length}" into one word when IFS holds no
         space, and a newer one on the PATH hides it from the kit;
  zsh    the script sourced after compinit, and the same file autoloaded
         from fpath under the name its #compdef line gives it;
  fish   `complete --do-complete`, as the kit does.

A shell that is not installed is skipped and named. Exit 1 with one line per
difference, 0 when none differs.
"""
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile

FALLBACK = ["zz-completion-alpha.txt", "zz-completion-beta.txt"]

BASH_HARNESS = r"""
source "$1" || exit 3
program=$2
spec=$(complete -p "$program" 2>/dev/null) || exit 4
fn=${spec##* -F }
fn=${fn%% *}
run() {
    COMP_WORDS=("${words[@]}")
    COMP_CWORD=$(( ${#words[@]} - 1 ))
    COMPREPLY=()
    "$fn" "$program" "${words[COMP_CWORD]}" "${words[COMP_CWORD-1]}"
    printf '<<CASE>>\n'
    for reply in "${COMPREPLY[@]}"; do printf '%s\n' "$reply"; done
}
words=("$program")
while IFS= read -r line || [ -n "$line" ]; do
    if [ "$line" = "<<RUN>>" ]; then
        run
        words=("$program")
    else
        words[${#words[@]}]="${line#=}"
    fi
done < "$3"
"""

# zsh completes inside the line editor only, so a second zsh runs on a
# pseudo-terminal of zsh's own and a widget prints what compadd received.
# $2 is `source FILE` or `fpath=(DIR $fpath)`: the two ways of loading.
ZSH_HARNESS = r"""
zmodload zsh/zpty || exit 3
load=$1 program=$2 cases=$3
zpty z 'TERM=dumb zsh -f -i' || exit 3
case $load in
    (source\ *) zpty -w z 'PS1=""; unsetopt zle_bracketed_paste 2>/dev/null; autoload -Uz compinit; compinit -u -D; '"$load" ;;
    (*) zpty -w z 'PS1=""; unsetopt zle_bracketed_paste 2>/dev/null; '"$load"'; autoload -Uz compinit; compinit -u -D' ;;
esac
zpty -w z 'compadd() { local a; for a in "$@"; do case $a in (-[OAD]*) builtin compadd "$@"; return ;; (--) break ;; esac; done; local -a __one; builtin compadd -O __one "$@"; local r=$?; __seen+=("${__one[@]}"); return r }'
zpty -w z '_probe() { typeset -ga __seen; __seen=(); _main_complete; print -r -- "<<BEG""IN>>"; print -rl -- $__seen; print -r -- "<<EN""D>>" }'
zpty -w z 'zle -C _probew complete-word _probe; bindkey "^B" _probew; bindkey "^U" kill-whole-line'
zpty -w z 'print REA""DY'
zpty -r z out '*READY*'
while IFS= read -r line; do
    zpty -n -w z "$program $line"$'\C-B'
    zpty -r z out '*<<END>>*'
    print -r -- "<<CASE>>"
    print -r -- "$out"
    zpty -n -w z $'\C-U'
done < $cases
zpty -d z
"""


def run(argv, env, cwd, timeout=60):
    try:
        return subprocess.run(argv, env=env, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                              text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def sections(text, begin, end):
    """The non-empty lines between `begin` and `end` of every <<CASE>> section."""
    found = []
    for chunk in text.split("<<CASE>>")[1:]:
        if begin:
            chunk = chunk.split(begin, 1)[1] if begin in chunk else ""
            chunk = chunk.split(end, 1)[0]
        found.append([line.strip() for line in chunk.replace("\r", "").split("\n") if line.strip()])
    return found


def bash_versions():
    """Every distinct bash: the PATH's, and /bin/bash, which macOS keeps at 3.2."""
    found = {}
    for candidate in (shutil.which("bash"), "/bin/bash"):
        if candidate and os.path.exists(candidate):
            found.setdefault(os.path.realpath(candidate), candidate)
    return list(found.values())


def drive(shell, how, script, name, cases, env, work):
    """What the script offers for each word list, or None and the reason the shell was not driven."""
    cwd = str(work / "files")
    lines = [" ".join(shlex.quote(word) for word in words[:-1]) + (" " if len(words) > 1 else "") + words[-1]
             for words in cases]
    if shell == "bash":
        (work / "cases").write_text("".join("".join(f"={word}\n" for word in words) + "<<RUN>>\n"
                                            for words in cases), encoding="utf-8")
        (work / "harness").write_text(BASH_HARNESS, encoding="utf-8")
        done = run([how, "--noprofile", "--norc", str(work / "harness"), str(script), name, str(work / "cases")],
                   env, cwd)
        if done is None or done.returncode != 0:
            return None, f"the harness failed: {done.stderr.strip()[:200] if done else 'no answer'}"
        return sections(done.stdout, "", ""), ""
    if shell == "zsh":
        (work / "cases").write_text("".join(line + "\n" for line in lines), encoding="utf-8")
        (work / "harness").write_text(ZSH_HARNESS, encoding="utf-8")
        if how == "sourced":
            load = f"source {shlex.quote(str(script))}"
        else:
            directory = work / "fpath"
            directory.mkdir(exist_ok=True)
            shutil.copyfile(script, directory / f"_{name}")
            load = f"fpath=({shlex.quote(str(directory))} $fpath)"
        done = run(["zsh", "-f", str(work / "harness"), load, name, str(work / "cases")], env, cwd)
        if done is None:
            return None, "the harness did not answer"
        if done.returncode == 3:
            return None, "skip: zsh has no zsh/zpty module"
        return sections(done.stdout, "<<BEGIN>>", "<<END>>"), ""
    answers = []
    for line in lines:
        done = run(["fish", "--no-config", "-c", "source $argv[1]; complete --do-complete=$argv[2]",
                    str(script), f"{name} {line}"], env, cwd)
        if done is None or done.returncode != 0:
            return None, f"fish failed: {done.stderr.strip()[:200] if done else 'no answer'}"
        answers.append([entry.split("\t", 1)[0] for entry in done.stdout.splitlines() if entry.strip()])
    return answers, ""


def check(label, command, failures, skipped):
    """Drive the three scripts of one product; returns the number of word lists compared."""
    def program(*words):
        return subprocess.run([*command, *words], check=True, capture_output=True, text=True,
                              env={**os.environ, "MAELYS_COMMANDS_PATH": "/nonexistent"}).stdout
    name = program("describe", "--field", "program").strip()
    cases = [[""], ["he"], ["help", ""], ["describe", ""], ["completion", ""], ["limits", "--level", ""],
             ["limits", "--le"], ["limits", "--level=h"], ["note", ""], ["greet", "--sh"],
             ["greet", "zz-completion-"], ["limits", "--tag", "zz-completion-"]]
    oracle = [program("__complete", "--", *words).split() for words in cases]
    compared = 0
    with tempfile.TemporaryDirectory(prefix="maelys-cli-completion-") as directory:
        work = pathlib.Path(directory)
        for part in ("bin", "files", "home"):
            (work / part).mkdir()
        for entry in FALLBACK:
            (work / "files" / entry).write_text("", encoding="utf-8")
        wrapper = work / "bin" / name
        wrapper.write_text(f"#!/bin/sh\nexec {shlex.join(command)} \"$@\"\n", encoding="utf-8")
        wrapper.chmod(0o755)
        env = {**os.environ, "PATH": str(work / "bin") + os.pathsep + os.environ.get("PATH", ""),
               "HOME": str(work / "home"), "ZDOTDIR": str(work / "home"),
               "XDG_CONFIG_HOME": str(work / "home" / ".config"), "TERM": "dumb",
               "MAELYS_COMMANDS_PATH": "/nonexistent"}
        runs = [("bash", how) for how in bash_versions()] + [("zsh", "sourced"), ("zsh", "autoloaded"),
                                                             ("fish", "sourced")]
        for shell, how in runs:
            where = f"{label} {shell} ({how})"
            if shell != "bash" and shutil.which(shell) is None:
                skipped.add(f"{shell} is not installed")
                continue
            script = work / f"completion.{shell}"
            script.write_text(program("completion", shell), encoding="utf-8")
            answers, note = drive(shell, how, script, name, cases, env, work)
            if answers is None and note.startswith("skip: "):
                skipped.add(note[6:])
                continue
            if answers is None or len(answers) != len(cases):
                failures.append(f"{where}: {note or f'{len(answers)} answers for {len(cases)} word lists'}")
                continue
            for words, expected, offered in zip(cases, oracle, answers):
                compared += 1
                if expected and set(offered) != set(expected):
                    failures.append(f"{where}: {' '.join(words)!r}: __complete returns {expected}, "
                                    f"the script offers {sorted(offered)}")
                elif not expected and not set(FALLBACK) <= set(offered):
                    failures.append(f"{where}: {' '.join(words)!r}: __complete returns nothing and the script "
                                    f"offers {sorted(offered)} instead of the files {FALLBACK}")
    return compared


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    failures, skipped = [], set()
    compared = check("C", [os.path.abspath(argv[1])], failures, skipped)
    compared += check("Python", [sys.executable, os.path.abspath(argv[2])], failures, skipped)
    for line in failures:
        print(f"completion-check: {line}", file=sys.stderr)
    if not failures:
        note = f"; skipped: {', '.join(sorted(skipped))}" if skipped else ""
        print(f"completion-check: ok ({compared} word lists in their shell{note})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
