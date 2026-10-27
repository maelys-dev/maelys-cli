// SPDX-License-Identifier: MPL-2.0
/** POSIX shell quoting, without substitution or execution of a shell. */
export function pagerArguments(setting: string | undefined): string[] | null {
  if (setting === undefined) return ["less"];
  const words: string[] = [];
  let token = "", quote = "", active = false;
  for (let i = 0; i < setting.length; i++) {
    const c = setting[i];
    if (quote === "'") {
      if (c === "'") quote = ""; else token += c;
    } else if (c === "\\" && quote !== "'") {
      if (++i >= setting.length) return null;
      const next = setting[i];
      if (quote === '"' && !['$', '"', "\\", "\n", "\x60"].includes(next)) token += "\\";
      if (next !== "\n") token += next;
      active = true;
    } else if (quote === '"') {
      if (c === '"') quote = ""; else token += c;
    } else if (c === "'" || c === '"') { quote = c; active = true; }
    else if (/\s/.test(c)) {
      if (active) { words.push(token); token = ""; active = false; }
    } else { token += c; active = true; }
  }
  if (quote) return null;
  if (active) words.push(token);
  return words.length ? words : null;
}
