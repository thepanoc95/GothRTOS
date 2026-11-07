import { describe, expect, test } from "bun:test";
import { mkdtempSync, readFileSync, rmSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import {
  BOARDS,
  REPO_ROOT,
  architectureFor,
  boardByKey,
  boardReady,
  devicesFor,
  discoverPorts,
  makefileTargets,
  makefileVariable,
  portSources,
  scaffoldPort,
} from "../src/registry";

describe("board registry", () => {
  test("contains every build target", () => {
    const keys = BOARDS.map((board) => board.key);
    for (const expected of ["um", "arm32", "rpi-zero", "pico", "pico2", "amd64"]) {
      expect(keys).toContain(expected);
    }
    expect(new Set(keys).size).toBe(keys.length);
  });

  test("resolves every board's architecture", () => {
    for (const board of BOARDS) {
      expect(architectureFor(board).key).toBe(board.arch);
    }
  });

  test("points every board at a real port", () => {
    for (const board of BOARDS) {
      expect(statSync(join(REPO_ROOT, board.portDir)).isDirectory()).toBe(true);
      if (board.linkerScript) {
        expect(statSync(join(REPO_ROOT, board.linkerScript)).isFile()).toBe(true);
      }
    }
  });

  test("embedded boards emit a SparseKernel image", () => {
    for (const board of BOARDS) {
      if (board.key === "um") {
        expect(board.sparseImage).toBeUndefined();
      } else {
        expect(board.sparseImage?.endsWith("SparseKernel.img3")).toBe(true);
      }
    }
  });

  test("resolves devices by compatible string", () => {
    for (const board of BOARDS) {
      expect(devicesFor(board).length).toBe(board.devices.length);
    }
    const pico2 = devicesFor(boardByKey("pico2"));
    expect(pico2.some((device) => device.compatible === "raspberrypi,rp2350-uart")).toBe(true);
  });

  test("reports readiness without invoking make", () => {
    for (const board of BOARDS) {
      expect(typeof boardReady(board)).toBe("boolean");
    }
  });
});

describe("GNUmakefile introspection", () => {
  test("finds board targets", () => {
    const targets = makefileTargets();
    for (const board of BOARDS) {
      expect(targets).toContain(board.makeTarget);
    }
    for (const target of ["pico", "pico2", "amd64", "arm32", "rpi-zero"]) {
      expect(targets).toContain(target);
    }
  });

  test("reads source lists", () => {
    expect(makefileVariable("PICO_C_SOURCES")).toContain("pe_rp2040.c");
    expect(makefileVariable("AMD64_C_SOURCES")).toContain("rtos/nanokernel.c");
    expect(makefileVariable("DOES_NOT_EXIST")).toBeNull();
  });
});

describe("port discovery", () => {
  test("finds the known ports", () => {
    const ports = discoverPorts();
    const names = ports.map((port) => port.name);
    for (const expected of ["arm", "amd64", "um"]) {
      expect(names).toContain(expected);
    }
    const arm = ports.find((port) => port.name === "arm");
    expect(arm?.sources.some((path) => path.endsWith("pe_cortexm.c"))).toBe(true);
  });

  test("lists port sources", () => {
    expect(portSources(boardByKey("amd64")).some((path) => path.endsWith("pe_amd64.c"))).toBe(true);
  });
});

describe("port scaffolding", () => {
  test("writes a skeleton", () => {
    const temporary = mkdtempSync(join(tmpdir(), "osbuild-"));
    try {
      const result = scaffoldPort("armv6m", "Test Board RP1", "0x20000000", temporary);
      const names = result.files.map((file) => file.split("/").pop());
      expect(names).toContain("pe_test_board_rp1.c");
      expect(names).toContain("test_board_rp1.ld");
      expect(names).toContain("README.md");

      const source = readFileSync(join(temporary, "pe_test_board_rp1.c"), "utf8");
      expect(source).toContain("pe_cortexm.h");
      expect(source).toContain("goth_cm_board_ops_t");
      expect(source).toContain("goth_cm_board_get_ops");
      expect(source).toContain("TEST_BOARD_RP1_UART_BASE");

      const linker = readFileSync(join(temporary, "test_board_rp1.ld"), "utf8");
      expect(linker).toContain(". = 0x20000000;");
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });

  test("rejects empty names", () => {
    expect(() => scaffoldPort("arm32", "   ")).toThrow();
  });

  test("rejects unknown architectures", () => {
    expect(() => scaffoldPort("sparc", "board")).toThrow();
  });
});
