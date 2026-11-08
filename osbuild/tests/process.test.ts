import { describe, expect, test } from "bun:test";

import { startRun, stripAnsi } from "../src/process";

describe("output streaming", () => {
  test("strips ANSI escape sequences", () => {
    expect(stripAnsi("\x1b[1;31mred\x1b[0m plain")).toBe("red plain");
  });

  test("streams stdout and stderr lines and reports the exit code", async () => {
    const lines: string[] = [];
    let code: number | null | undefined;

    const run = startRun(
      {
        argv: [
          "sh",
          "-c",
          "printf 'one\\ntwo\\n'; printf 'err\\n' >&2; exit 3",
        ],
        cwd: process.cwd(),
      },
      {
        onLine: (line) => lines.push(line),
        onDone: (exitCode) => {
          code = exitCode;
        },
      },
    );

    await run.exited;
    expect(lines).toContain("one");
    expect(lines).toContain("two");
    expect(lines).toContain("err");
    expect(code).toBe(3);
  });
});
