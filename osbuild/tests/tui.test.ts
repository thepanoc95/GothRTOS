import { describe, expect, test } from "bun:test";

import { createTestRenderer, type MockInput, type TestRenderer } from "@opentui/core/testing";

import { createBuildApp, type BuildApp } from "../src/tui";

interface Harness {
  renderer: TestRenderer;
  mockInput: MockInput;
  frame: () => string;
  render: () => Promise<void>;
}

async function withApp(
  run: (harness: Harness) => Promise<void>,
  options: { kittyKeyboard?: boolean; height?: number } = {},
): Promise<void> {
  const setup = await createTestRenderer({
    width: 100,
    height: options.height ?? 40,
    kittyKeyboard: options.kittyKeyboard ?? true,
  });
  const app: BuildApp = createBuildApp(setup.renderer);
  const harness: Harness = {
    renderer: setup.renderer,
    mockInput: setup.mockInput,
    frame: () => setup.captureCharFrame(),
    render: () => setup.renderOnce(),
  };

  try {
    await harness.render();
    await run(harness);
  } finally {
    app.destroy();
    await setup.dispose();
  }
}

describe("build TUI", () => {
  test("renders the board view on start", async () => {
    await withApp(async ({ frame }) => {
      const text = frame();
      expect(text).toContain("GothRTOS System Builder");
      expect(text).toContain("1 Boards");
      expect(text).toContain("User mode (host)");
      expect(text).toContain("SUPPORT");
      expect(text).toContain("Output");
      expect(text).toContain("run: idle");
    });
  });

  test("switches views with the number keys", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("2");
      await render();
      expect(frame()).toContain("Platform experts");

      mockInput.pressKey("3");
      await render();
      expect(frame()).toContain("Cross-compiled targets work from any host");

      mockInput.pressKey("1");
      await render();
      expect(frame()).toContain("SUPPORT");

      mockInput.pressKey("4");
      await render();
      expect(frame()).toContain("New port");
      expect(frame()).toContain("Architecture");
    });
  });

  test("cycles views with the bracket keys", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("]");
      await render();
      expect(frame()).toContain("Platform experts");

      mockInput.pressKey("[");
      await render();
      expect(frame()).toContain("SUPPORT");
    });
  });

  test("moves the board selection with j and k", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("j");
      await render();
      const arm32 = frame();
      expect(arm32).toContain("ARM32 QEMU virt");
      expect(arm32).toContain("arm32 / make arm32");

      mockInput.pressKey("k");
      await render();
      expect(frame()).toContain("um / make all");
    });
  });

  test("reports a missing scaffold board name", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("4");
      await render();
      mockInput.pressKey("s", { ctrl: true });
      await render();
      expect(frame()).toContain("board name is required");
    });
  });

  test("returns from the scaffold form with escape", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("4");
      await render();
      expect(frame()).toContain("New port");

      mockInput.pressEscape();
      await render();
      expect(frame()).toContain("SUPPORT");
    });
  });

  test("accepts text in the scaffold form", async () => {
    await withApp(async ({ mockInput, render, frame }) => {
      mockInput.pressKey("4");
      await render();
      await mockInput.typeText("Pico 2 W");
      await render();
      expect(frame()).toContain("Pico 2 W");
    });
  });

  test("can be destroyed without a running command", async () => {
    const setup = await createTestRenderer({ width: 80, height: 24 });
    const app = createBuildApp(setup.renderer);
    await setup.renderOnce();
    app.destroy();
    await expect(setup.dispose()).resolves.toBeUndefined();
  });
});
