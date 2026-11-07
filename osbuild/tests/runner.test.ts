import { describe, expect, test } from "bun:test";

import { boardByKey } from "../src/registry";
import {
  boardsForArch,
  buildCommand,
  cleanCommand,
  commandDisplay,
  environmentMatrix,
  makeAvailable,
  runCommand,
  testCommand,
} from "../src/runner";

describe("make commands", () => {
  test("builds with jobs and extra arguments", () => {
    const command = buildCommand(boardByKey("pico"), 4, "V=1");
    expect(command.argv.slice(0, 3)).toEqual(["make", "-j4", "pico"]);
    expect(command.argv).toContain("V=1");
    expect(commandDisplay(command)).toContain("make -j4 pico");
  });

  test("uses the board test target", () => {
    expect(testCommand(boardByKey("amd64")).argv).toEqual(["make", "test-amd64"]);
  });

  test("refuses boards without a run target", () => {
    expect(() => runCommand(boardByKey("pico"))).toThrow();
  });

  test("cleans the tree", () => {
    expect(cleanCommand().argv).toEqual(["make", "clean"]);
  });

  test("detects make on PATH", () => {
    expect(typeof makeAvailable()).toBe("boolean");
  });
});

describe("environment matrix", () => {
  test("covers the known architectures", () => {
    const rows = environmentMatrix("linux");
    const arches = rows.map((row) => row.arch);
    expect(arches).toContain("um");
    expect(arches).toContain("arm32");
    expect(arches).toContain("amd64");
    for (const row of rows) expect(row.toolchain.length).toBeGreaterThan(0);
  });

  test("ties user mode to the host platform", () => {
    const linux = environmentMatrix("linux").find((row) => row.arch === "um");
    const plan9 = environmentMatrix("plan9").find((row) => row.arch === "um");
    expect(linux?.hostOk).toBe(true);
    expect(plan9?.hostOk).toBe(false);
  });

  test("maps boards to architectures", () => {
    expect(boardsForArch("arm32").map((board) => board.key)).toEqual([
      "arm32",
      "rpi-zero",
    ]);
  });
});
