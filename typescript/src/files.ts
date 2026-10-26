// SPDX-License-Identifier: MPL-2.0
import { constants, closeSync, fstatSync, fsyncSync, linkSync, openSync, readSync, renameSync, unlinkSync, writeSync } from "node:fs";
import { createHash, randomUUID } from "node:crypto";
import path from "node:path";
import { Failure } from "./values.js";

/** Read the descriptor actually opened, bounded and regular, without following its last symlink. */
export function readRegularFile(file: string, minimum: number, maximum: number): Buffer {
  if (process.platform === "win32") throw new Failure("UNSUPPORTED", "The bounded file primitive requires POSIX no-follow descriptors.");
  if (!Number.isSafeInteger(minimum) || !Number.isSafeInteger(maximum) || minimum < 0 || maximum < minimum) throw new TypeError("Invalid file bounds.");
  const fd = openSync(file, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
  try {
    const stat = fstatSync(fd);
    if (!stat.isFile()) throw new Failure("PRECONDITION_FAILED", file + " is not a regular file.");
    if (stat.size > maximum) throw new Failure("PRECONDITION_FAILED", file + " exceeds its maximum size.");
    const chunks: Buffer[] = [];
    let total = 0;
    while (true) {
      const chunk = Buffer.alloc(Math.min(65536, maximum - total + 1));
      const size = readSync(fd, chunk, 0, chunk.length, null);
      if (!size) break;
      total += size;
      if (total > maximum) throw new Failure("PRECONDITION_FAILED", file + " exceeds its maximum size.");
      chunks.push(chunk.subarray(0, size));
    }
    if (total < minimum) throw new Failure("PRECONDITION_FAILED", file + " is below its minimum size.");
    return Buffer.concat(chunks, total);
  } finally { closeSync(fd); }
}
/** Atomic replace, or a no-replace link that fails if another writer created the destination. */
export function writeFileAtomic(file: string, data: Uint8Array, mode: number, policy: "replace" | "no-replace"): void {
  if (process.platform === "win32") throw new Failure("UNSUPPORTED", "The atomic file primitive requires POSIX link and rename semantics.");
  if (!["replace", "no-replace"].includes(policy)) throw new TypeError("An atomic write requires an explicit replace policy.");
  const directory = path.dirname(file), temporary = path.join(directory, "." + path.basename(file) + "." + randomUUID() + ".tmp");
  let fd: number | undefined;
  try {
    fd = openSync(temporary, constants.O_WRONLY | constants.O_CREAT | constants.O_EXCL | constants.O_NOFOLLOW, mode);
    let offset = 0;
    while (offset < data.byteLength) offset += writeSync(fd, data, offset, data.byteLength - offset);
    fsyncSync(fd); closeSync(fd); fd = undefined;
    if (policy === "replace") renameSync(temporary, file);
    else { linkSync(temporary, file); unlinkSync(temporary); }
    const parent = openSync(directory, constants.O_RDONLY);
    try { fsyncSync(parent); } finally { closeSync(parent); }
  } finally {
    if (fd !== undefined) closeSync(fd);
    try { unlinkSync(temporary); } catch (error) { if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error; }
  }
}
export function fileFailure(error: NodeJS.ErrnoException, what = error.path ?? "File operation"): Failure {
  const code = error.code === "ENOENT" ? "NOT_FOUND" : ["EACCES", "EPERM"].includes(error.code ?? "") ? "ACCESS_DENIED" : ["EINVAL", "ELOOP", "EISDIR", "ENOTDIR", "EEXIST", "EMLINK"].includes(error.code ?? "") ? "PRECONDITION_FAILED" : "IO_FAILED";
  return new Failure(code, what + ": " + error.message, "Check the named path and its permissions.");
}
/** The C/Python framing, including a presence byte and big-endian u64 lengths. */
export class Fingerprint {
  private readonly hash = createHash("sha256");
  private finished = false;
  add(label: string, value: string | Uint8Array | null): this {
    if (this.finished) throw new TypeError("Fingerprint was already finalized.");
    const name = Buffer.from(label), data = value === null ? Buffer.alloc(0) : typeof value === "string" ? Buffer.from(value) : value;
    const length = (n: number) => { const bytes = Buffer.alloc(8); bytes.writeBigUInt64BE(BigInt(n)); return bytes; };
    this.hash.update(length(name.length)).update(name).update(Buffer.from([value === null ? 0 : 1])).update(length(data.byteLength)).update(data);
    return this;
  }
  addFile(label: string, file: string, maximum: number): this {
    try { return this.add(label, createHash("sha256").update(readRegularFile(file, 0, maximum)).digest("hex")); }
    catch (error) { if ((error as NodeJS.ErrnoException).code === "ENOENT") return this.add(label, null); throw error; }
  }
  finish(): string {
    if (this.finished) throw new TypeError("Fingerprint was already finalized.");
    this.finished = true; return "sha256:" + this.hash.digest("hex");
  }
}
