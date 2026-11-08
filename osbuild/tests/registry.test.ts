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
  scaffoldDriver,
  scaffoldPort,
} from "../src/registry";

describe("board registry", () => {
  test("contains every build target", () => {
    const keys = BOARDS.map((board) => board.key);
    for (const expected of ["um", "arm32", "rpi-zero"]) {
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

  test("boards only advertise images produced by this checkout", () => {
    for (const board of BOARDS) {
      expect(board.sparseImage).toBeUndefined();
    }
  });

  test("resolves devices by compatible string", () => {
    for (const board of BOARDS) {
      expect(devicesFor(board).length).toBe(board.devices.length);
    }
    const rpi = devicesFor(boardByKey("rpi-zero"));
    expect(rpi.some((device) => device.compatible === "brcm,bcm2835-pl011")).toBe(true);
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
    for (const target of ["arm32", "rpi-zero"]) {
      expect(targets).toContain(target);
    }
    expect(targets).not.toContain("pico");
    expect(targets).not.toContain("amd64");
  });

  test("reads source lists and ignores absent targets", () => {
    expect(makefileVariable("ARM_C_SOURCES")).toContain("pe_arm32.c");
    expect(makefileVariable("PICO_C_SOURCES")).toBeNull();
    expect(makefileVariable("DOES_NOT_EXIST")).toBeNull();
  });
});

describe("port discovery", () => {
  test("finds the known ports", () => {
    const ports = discoverPorts();
    const names = ports.map((port) => port.name);
    for (const expected of ["arm", "um"]) {
      expect(names).toContain(expected);
    }
    expect(portSources(boardByKey("arm32")).some((path) => path.endsWith("pe_arm32.c"))).toBe(true);
  });
});

describe("port scaffolding", () => {
  test("writes a skeleton", () => {
    const temporary = mkdtempSync(join(tmpdir(), "osbuild-"));
    try {
      const result = scaffoldPort("arm32", "Test Board RP1", "0x20000000", temporary);
      const names = result.files.map((file) => file.split("/").pop());
      expect(names).toContain("pe_test_board_rp1.c");
      expect(names).toContain("test_board_rp1.ld");
      expect(names).toContain("README.md");

      const source = readFileSync(join(temporary, "pe_test_board_rp1.c"), "utf8");
      expect(source).toContain("pe_arm32.h");
      expect(source).toContain("goth_arm_board_ops_t");
      expect(source).toContain("goth_arm_board_get_ops");
      expect(source).toContain("TEST_BOARD_RP1_UART_BASE");
      expect(source).not.toContain("gothrtos/hdf.h");

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

describe("driver scaffolding", () => {
  test("writes an initialized driver using the kernel driver model", () => {
    const temporary = mkdtempSync(join(tmpdir(), "osbuild-driver-"));
    try {
      const result = scaffoldDriver(
        "bcm2835-uart",
        ["brcm,bcm2835-pl011", "brcm,pl011"],
        temporary,
      );
      const source = readFileSync(result.file, "utf8");
      expect(result.file).toBe(join(temporary, "goth_bcm2835_uart.c"));
      expect(source).toContain('#include "gothrtos/driver.h"');
      expect(source).toContain('"brcm,bcm2835-pl011"');
      expect(source).toContain('"brcm,pl011"');
      expect(source).toContain("goth_bcm2835_uart_driver_init");
      expect(source).toContain("GOTH_STATUS_PLATFORM_ERROR");
      expect(() =>
        scaffoldDriver("bcm2835-uart", ["brcm,bcm2835-pl011"], temporary),
      ).toThrow("already exists");
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });

  test("rejects empty names and missing or unsafe compatible strings", () => {
    const temporary = mkdtempSync(join(tmpdir(), "osbuild-driver-"));
    try {
      expect(() => scaffoldDriver("../uart", ["brcm,uart"], temporary)).toThrow();
      expect(() => scaffoldDriver("uart", [], temporary)).toThrow();
      expect(() => scaffoldDriver("uart", ['brcm,"uart'], temporary)).toThrow();
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });
});
