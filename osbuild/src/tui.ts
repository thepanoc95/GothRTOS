/**
 * GothRTOS System Builder TUI.
 *
 * An OpenTUI front end over the non-interactive registry and runner. It keeps
 * the same feature set as the previous front end: board inspection, support
 * checks, live make output, environment matrix, port discovery and port
 * scaffolding.
 *
 * The imperative tree is created once; views and output are retained
 * renderables whose content is updated in place.
 */

import {
  BoxRenderable,
  InputRenderable,
  InputRenderableEvents,
  ScrollBoxRenderable,
  SelectRenderable,
  SelectRenderableEvents,
  TabSelectRenderable,
  TextAttributes,
  TextRenderable,
  createCliRenderer,
  type CliRenderer,
  type KeyEvent,
  type Renderable,
} from "@opentui/core";
import { availableParallelism } from "node:os";
import { relative } from "node:path";

import {
  BOARDS,
  REPO_ROOT,
  architectureFor,
  boardSupport,
  devicesFor,
  discoverPorts,
  makefileTargets,
  portSources,
  scaffoldPort,
  type Board,
} from "./registry";
import {
  buildCommand,
  cleanCommand,
  commandDisplay,
  currentHostPlatform,
  environmentMatrix,
  makeAvailable,
  runCommand,
  testCommand,
  type Command,
} from "./runner";
import { startRun, type RunHandle } from "./process";

// --------------------------------------------------------------------------- //
// Palette (borrowed from assets/butterfly.so.css)
// --------------------------------------------------------------------------- //

const C = {
  bg: "#13171c",
  panel: "#1d1d1d",
  panelAlt: "#22252c",
  border: "#4c5164",
  text: "#e1ebf7",
  muted: "#77808c",
  accent: "#ffd330",
  green: "#b0ff4f",
  purple: "#cba6f7",
  blue: "#9ba7b5",
  orange: "#ff9f67",
  red: "#ff6b6b",
} as const;

const VIEWS = ["boards", "ports", "environment", "scaffold"] as const;
export type View = (typeof VIEWS)[number];

const VIEW_TITLES: Record<View, string> = {
  boards: "Boards",
  ports: "Ports",
  environment: "Environment",
  scaffold: "New port",
};

const TAB_LABELS: Record<View, string> = {
  boards: "1 Boards",
  ports: "2 Ports",
  environment: "3 Environment",
  scaffold: "4 Scaffold",
};

const SCAFFOLD_ARCHES = [
  ["arm32", "ARM32 (A-profile)"],
] as const;

const MAX_OUTPUT_LINES = 5_000;

// --------------------------------------------------------------------------- //
// Public API
// --------------------------------------------------------------------------- //

export interface BuildAppOptions {
  jobs?: number;
  extraArgs?: string;
}

export interface BuildApp {
  setView: (view: View) => void;
  render: () => void;
  destroy: () => void;
}

/**
 * Builds the TUI tree on an existing renderer. Tests call this with an
 * OpenTUI test renderer; `runTui` calls it with a real terminal renderer.
 */
export function createBuildApp(
  renderer: CliRenderer,
  options: BuildAppOptions = {},
): BuildApp {
  const state = {
    view: "boards" as View,
    boardIndex: 0,
    jobs: options.jobs ?? Math.max(1, Math.min(64, availableParallelism())),
    extraArgs: options.extraArgs ?? "",
    output: [] as string[],
    runStatus: "idle",
    runBusy: false,
    activeRun: null as RunHandle | null,
    message: "",
    scaffoldArch: SCAFFOLD_ARCHES[0][0] as string,
    scaffoldBoard: "",
    scaffoldAddress: "0x20000000",
  };

  const text = (
    content: string,
    fg: string = C.text,
    attributes = 0,
  ): TextRenderable =>
    new TextRenderable(renderer, {
      content,
      fg,
      attributes,
      wrapMode: "char",
      selectable: false,
    });

  const scroll = (): ScrollBoxRenderable =>
    new ScrollBoxRenderable(renderer, {
      width: "100%",
      flexGrow: 1,
      minHeight: 0,
      focusable: false,
      backgroundColor: C.panel,
      viewportOptions: { backgroundColor: C.panel },
      contentOptions: { backgroundColor: C.panel },
      scrollbarOptions: {
        trackOptions: {
          foregroundColor: C.border,
          backgroundColor: C.panel,
        },
      },
    });

  const body = (): BoxRenderable =>
    new BoxRenderable(renderer, {
      width: "100%",
      flexDirection: "column",
      padding: 1,
      gap: 0,
      backgroundColor: C.panel,
    });

  const clearBody = (container: BoxRenderable): void => {
    for (const child of container.getChildren()) {
      child.destroyRecursively();
    }
  };

  // -- header --------------------------------------------------------------- //

  const header = new BoxRenderable(renderer, {
    id: "header",
    width: "100%",
    height: 3,
    border: true,
    borderStyle: "rounded",
    borderColor: C.border,
    backgroundColor: C.panel,
    title: " GothRTOS",
    titleColor: C.accent,
    flexDirection: "row",
    justifyContent: "space-between",
    alignItems: "center",
    paddingLeft: 1,
    paddingRight: 1,
  });
  const headerTitle = text(
    "OSBuild System :3",
    C.text,
    TextAttributes.BOLD,
  );
  const headerMeta = text("", C.muted);
  header.add(headerTitle);
  header.add(headerMeta);

  // -- tabs ----------------------------------------------------------------- //

  const tabs = new TabSelectRenderable(renderer, {
    id: "tabs",
    width: "100%",
    height: 2,
    tabWidth: 16,
    showDescription: false,
    showUnderline: true,
    options: VIEWS.map((view) => ({
      name: TAB_LABELS[view],
      description: "",
      value: view,
    })),
    backgroundColor: C.bg,
    textColor: C.muted,
    focusedBackgroundColor: C.bg,
    focusedTextColor: C.text,
    selectedBackgroundColor: C.panelAlt,
    selectedTextColor: C.accent,
    selectedDescriptionColor: C.muted,
  });
  tabs.focusable = false;

  // -- main split ----------------------------------------------------------- //

  const main = new BoxRenderable(renderer, {
    id: "main",
    width: "100%",
    flexGrow: 1,
    minHeight: 0,
    flexDirection: "row",
    gap: 1,
    backgroundColor: C.bg,
  });

  const sidebar = new BoxRenderable(renderer, {
    id: "sidebar",
    width: 36,
    border: true,
    borderStyle: "rounded",
    borderColor: C.border,
    backgroundColor: C.panel,
    title: " Boards ",
    titleColor: C.accent,
  });

  const boardSelect = new SelectRenderable(renderer, {
    id: "board-select",
    width: "100%",
    height: "100%",
    showDescription: true,
    showScrollIndicator: true,
    wrapSelection: false,
    options: BOARDS.map((board) => ({
      name: board.display,
      description: `${board.arch} · make ${board.makeTarget}`,
      value: board.key,
    })),
    backgroundColor: C.panel,
    focusedBackgroundColor: C.panel,
    textColor: C.text,
    focusedTextColor: C.text,
    selectedBackgroundColor: C.panelAlt,
    selectedTextColor: C.accent,
    descriptionColor: C.muted,
    selectedDescriptionColor: C.blue,
  });
  boardSelect.focusable = false;
  sidebar.add(boardSelect);

  const contentArea = new BoxRenderable(renderer, {
    id: "content",
    flexGrow: 1,
    minHeight: 0,
    border: true,
    borderStyle: "rounded",
    borderColor: C.border,
    backgroundColor: C.panel,
    title: " Boards ",
    titleColor: C.accent,
  });

  const boardsScroll = scroll();
  const boardsBody = body();
  boardsScroll.add(boardsBody);

  const portsScroll = scroll();
  const portsBody = body();
  portsScroll.add(portsBody);

  const environmentScroll = scroll();
  const environmentBody = body();
  environmentScroll.add(environmentBody);

  const scaffoldScroll = scroll();
  const scaffoldBox = new BoxRenderable(renderer, {
    id: "scaffold",
    width: "100%",
    flexDirection: "column",
    padding: 1,
    gap: 1,
    backgroundColor: C.panel,
  });
  scaffoldScroll.add(scaffoldBox);

  contentArea.add(boardsScroll);
  contentArea.add(portsScroll);
  contentArea.add(environmentScroll);
  contentArea.add(scaffoldScroll);

  // -- scaffold form -------------------------------------------------------- //

  const scaffoldIntro = text(
    "Write a platform-expert skeleton under pexpert/. Generated files are not wired into the GNUmakefile, so they cannot break an existing build.",
    C.muted,
  );
  scaffoldIntro.width = "100%";
  const scaffoldArchLabel = text("Architecture", C.purple, TextAttributes.BOLD);
  const scaffoldArchSelect = new SelectRenderable(renderer, {
    id: "scaffold-arch",
    width: 34,
    height: 5,
    options: SCAFFOLD_ARCHES.map(([value, label]) => ({
      name: label,
      description: "",
      value,
    })),
    showDescription: false,
    showScrollIndicator: false,
    wrapSelection: true,
    backgroundColor: C.panelAlt,
    focusedBackgroundColor: C.panelAlt,
    textColor: C.text,
    focusedTextColor: C.text,
    selectedBackgroundColor: C.border,
    selectedTextColor: C.accent,
    descriptionColor: C.muted,
    selectedDescriptionColor: C.blue,
  });
  const scaffoldBoardLabel = text("Board name", C.purple, TextAttributes.BOLD);
  const scaffoldBoardInput = new InputRenderable(renderer, {
    id: "scaffold-board",
    width: 52,
    placeholder: "e.g. Pico 2 W (RP2350)",
    backgroundColor: C.panelAlt,
    focusedBackgroundColor: C.panelAlt,
    textColor: C.text,
    cursorColor: C.accent,
  });
  const scaffoldAddressLabel = text("Load address", C.purple, TextAttributes.BOLD);
  const scaffoldAddressInput = new InputRenderable(renderer, {
    id: "scaffold-address",
    width: 24,
    value: state.scaffoldAddress,
    backgroundColor: C.panelAlt,
    focusedBackgroundColor: C.panelAlt,
    textColor: C.text,
    cursorColor: C.accent,
  });
  const scaffoldHint = text(
    "Ctrl+S scaffold port   ·   Tab next field   ·   Esc back to boards",
    C.muted,
  );
  scaffoldHint.width = "100%";
  const scaffoldResult = text("", C.green);
  scaffoldResult.width = "100%";

  scaffoldBox.add(scaffoldIntro);
  scaffoldBox.add(scaffoldResult);
  scaffoldBox.add(scaffoldArchLabel);
  scaffoldBox.add(scaffoldArchSelect);
  scaffoldBox.add(scaffoldBoardLabel);
  scaffoldBox.add(scaffoldBoardInput);
  scaffoldBox.add(scaffoldAddressLabel);
  scaffoldBox.add(scaffoldAddressInput);
  scaffoldBox.add(scaffoldHint);

  scaffoldArchSelect.on(
    SelectRenderableEvents.SELECTION_CHANGED,
    (index: number) => {
      state.scaffoldArch = SCAFFOLD_ARCHES[index]?.[0] ?? SCAFFOLD_ARCHES[0][0];
    },
  );
  scaffoldBoardInput.on(InputRenderableEvents.INPUT, (value: string) => {
    state.scaffoldBoard = value;
  });
  scaffoldAddressInput.on(InputRenderableEvents.INPUT, (value: string) => {
    state.scaffoldAddress = value;
  });

  // -- output --------------------------------------------------------------- //

  const outputBox = new BoxRenderable(renderer, {
    id: "output",
    width: "100%",
    height: 10,
    border: true,
    borderStyle: "rounded",
    borderColor: C.border,
    backgroundColor: C.panel,
    title: " Output ",
    titleColor: C.accent,
    bottomTitle:
      " b build · r run · t test · c clean · x cancel · C clear · q quit ",
  });
  const outputScroll = new ScrollBoxRenderable(renderer, {
    id: "output-scroll",
    width: "100%",
    flexGrow: 1,
    minHeight: 0,
    focusable: false,
    stickyScroll: true,
    stickyStart: "bottom",
    backgroundColor: C.panel,
    viewportOptions: { backgroundColor: C.panel },
    contentOptions: {
      backgroundColor: C.panel,
      paddingLeft: 1,
      paddingRight: 1,
    },
  });
  const outputText = new TextRenderable(renderer, {
    id: "output-text",
    content: "",
    fg: C.text,
    wrapMode: "char",
    selectable: false,
  });
  outputScroll.add(outputText);
  outputBox.add(outputScroll);

  const statusText = text("", C.muted);
  const statusBar = new BoxRenderable(renderer, {
    id: "status",
    width: "100%",
    height: 1,
    flexDirection: "row",
    paddingLeft: 1,
    paddingRight: 1,
    backgroundColor: C.bg,
  });
  statusBar.add(statusText);

  renderer.root.add(header);
  renderer.root.add(tabs);
  renderer.root.add(main);
  main.add(sidebar);
  main.add(contentArea);
  renderer.root.add(outputBox);
  renderer.root.add(statusBar);

  // ----------------------------------------------------------------------- //
  // Rendering
  // ----------------------------------------------------------------------- //

  const selectedBoard = (): Board => BOARDS[state.boardIndex] ?? BOARDS[0];

  function renderBoards(): void {
    const board = selectedBoard();
    const arch = architectureFor(board);
    const support = boardSupport(board);
    const ready = support.every((row) => row.ok);
    const devices = devicesFor(board);
    const sources = portSources(board);

    clearBody(boardsBody);

    boardsBody.add(text(board.display, C.accent, TextAttributes.BOLD));
    boardsBody.add(
      text(
        `${board.key} / make ${board.makeTarget} / ${arch.display}`,
        C.muted,
      ),
    );
    if (board.notes) boardsBody.add(text(board.notes, C.text));
    boardsBody.add(text(""));
    boardsBody.add(
      text(
        `Status  ${ready ? "READY" : "NEEDS SETUP"}`,
        ready ? C.green : C.orange,
        TextAttributes.BOLD,
      ),
    );

    boardsBody.add(text(""));
    boardsBody.add(text("SUPPORT", C.purple, TextAttributes.BOLD));
    for (const row of support) {
      boardsBody.add(
        text(
          `${row.ok ? "✓" : "!"} ${row.check.padEnd(14)} ${row.detail}`,
          row.ok ? C.green : C.orange,
        ),
      );
    }

    boardsBody.add(text(""));
    boardsBody.add(text("TARGET", C.purple, TextAttributes.BOLD));
    boardsBody.add(text(`Port         ${board.portDir}`));
    boardsBody.add(text(`Build dir    ${board.buildDir}`));
    boardsBody.add(text(`Image        ${board.image}`));
    if (board.sparseImage) {
      boardsBody.add(text(`SparseKernel ${board.sparseImage}`));
    }
    boardsBody.add(text(`Linker       ${board.linkerScript || "(host)"}`));
    if (board.deviceConfig) {
      boardsBody.add(text(`Device cfg   ${board.deviceConfig}`));
    }

    boardsBody.add(text(""));
    boardsBody.add(text("COMMANDS", C.purple, TextAttributes.BOLD));
    boardsBody.add(
      text(
        `Build  ${commandDisplay(buildCommand(board, state.jobs, state.extraArgs))}`,
        C.blue,
      ),
    );
    if (board.runTarget) {
      boardsBody.add(
        text(
          `Run    ${commandDisplay(runCommand(board, state.extraArgs))}`,
          C.blue,
        ),
      );
    }
    if (board.testTarget) {
      boardsBody.add(
        text(
          `Test   ${commandDisplay(testCommand(board, state.extraArgs))}`,
          C.blue,
        ),
      );
    }

    boardsBody.add(text(""));
    boardsBody.add(
      text(`DEVICES (${devices.length})`, C.purple, TextAttributes.BOLD),
    );
    if (devices.length === 0) {
      boardsBody.add(text("this port publishes no devices", C.muted));
    }
    for (const device of devices) {
      boardsBody.add(
        text(
          `• ${device.name} (${device.category}, ${device.compatible})`,
        ),
      );
    }

    boardsBody.add(text(""));
    boardsBody.add(
      text(`PORT SOURCES (${sources.length})`, C.purple, TextAttributes.BOLD),
    );
    if (sources.length === 0) {
      boardsBody.add(text("(no port sources found)", C.muted));
    }
    for (const source of sources) {
      boardsBody.add(text(source, C.muted));
    }
  }

  function renderPorts(): void {
    clearBody(portsBody);
    const ports = discoverPorts();

    portsBody.add(
      text(
        "Platform experts discovered under pexpert/. Ports without a board entry in registry.ts are not buildable from here.",
        C.muted,
      ),
    );
    portsBody.add(text(""));
    portsBody.add(
      text(
        "PORT".padEnd(18) +
          "ARCH".padEnd(10) +
          "C".padStart(4) +
          "ASM".padStart(5) +
          "LD".padStart(4) +
          "H".padStart(4),
        C.purple,
        TextAttributes.BOLD,
      ),
    );
    for (const port of ports) {
      portsBody.add(
        text(
          port.directory.padEnd(18) +
            port.arch.padEnd(10) +
            String(port.sources.length).padStart(4) +
            String(port.assembly.length).padStart(5) +
            String(port.linkerScripts.length).padStart(4) +
            String(port.headers.length).padStart(4),
        ),
      );
    }
    if (ports.length === 0) portsBody.add(text("No ports found.", C.muted));
  }

  function renderEnvironment(): void {
    clearBody(environmentBody);
    const rows = environmentMatrix();

    environmentBody.add(
      text(
        "Cross-compiled targets work from any host. The user-mode build is tied to UM_PLATFORM.",
        C.muted,
      ),
    );
    environmentBody.add(text(""));
    for (const row of rows) {
      environmentBody.add(
        text(
          `${row.toolchainOk ? "✓" : "!"} ${row.display}`,
          row.toolchainOk ? C.green : C.orange,
          TextAttributes.BOLD,
        ),
      );
      environmentBody.add(
        text(
          `   ${row.toolchain} · ${row.hostOk ? "host supported" : "host unsupported"} · ${row.note}`,
          C.muted,
        ),
      );
    }

    environmentBody.add(text(""));
    environmentBody.add(text("MAKE TARGETS", C.purple, TextAttributes.BOLD));
    const targets = makefileTargets();
    environmentBody.add(
      text(targets.length > 0 ? targets.join("  ") : "none found", C.blue),
    );
  }

  function updateOutput(): void {
    outputText.content = state.output.join("\n");
  }

  function updateStatus(): void {
    const host = `${currentHostPlatform()} · Bun ${Bun.version} · make ${
      makeAvailable() ? "found" : "missing"
    }`;
    headerMeta.content = host;
    statusText.content = [
      state.message || "ready",
      `run: ${state.runStatus}`,
      `jobs: ${state.jobs}`,
      `repo: ${REPO_ROOT}`,
    ].join("   ·   ");
  }

  // ----------------------------------------------------------------------- //
  // Actions
  // ----------------------------------------------------------------------- //

  function actionCommand(
    action: "build" | "run" | "test" | "clean",
  ): Command | null {
    const board = selectedBoard();
    switch (action) {
      case "build":
        return buildCommand(board, state.jobs, state.extraArgs);
      case "run":
        return board.runTarget ? runCommand(board, state.extraArgs) : null;
      case "test":
        return board.testTarget ? testCommand(board, state.extraArgs) : null;
      case "clean":
        return cleanCommand();
    }
  }

  function runAction(action: "build" | "run" | "test" | "clean"): void {
    if (state.runBusy) {
      state.message = "a command is already running";
      updateStatus();
      return;
    }
    if (!makeAvailable()) {
      state.message = "make is not on PATH; cannot run the build";
      updateStatus();
      return;
    }

    let command: Command | null;
    try {
      command = actionCommand(action);
    } catch (error) {
      state.message = error instanceof Error ? error.message : String(error);
      updateStatus();
      return;
    }
    if (!command) {
      state.message = `${selectedBoard().display} has no ${action} target`;
      updateStatus();
      return;
    }

    state.output = [`$ ${commandDisplay(command)}`];
    state.runBusy = true;
    state.runStatus = `running ${action}…`;
    state.message = "";
    outputBox.title = ` Output — ${action} `;
    updateOutput();
    updateStatus();

    state.activeRun = startRun(command, {
      onLine: (line) => {
        state.output.push(line);
        if (state.output.length > MAX_OUTPUT_LINES) {
          state.output.splice(0, state.output.length - MAX_OUTPUT_LINES);
        }
        updateOutput();
      },
      onDone: (code) => {
        state.runBusy = false;
        state.activeRun = null;
        state.runStatus = code === 0 ? `${action} ok` : `${action} exit ${code ?? "?"}`;
        state.message = code === 0 ? "" : `${action} failed`;
        updateStatus();
      },
      onError: (error) => {
        state.output.push(
          `runner error: ${error instanceof Error ? error.message : String(error)}`,
        );
        updateOutput();
      },
    });
  }

  function cancelRun(): void {
    if (!state.activeRun) return;
    state.activeRun.kill();
    state.runStatus = "cancelling…";
    updateStatus();
  }

  function clearOutput(): void {
    if (state.runBusy) {
      state.message = "cannot clear output while a command is running";
      updateStatus();
      return;
    }
    state.output = [];
    state.runStatus = "idle";
    state.activeRun = null;
    outputBox.title = " Output ";
    updateOutput();
    updateStatus();
  }

  function submitScaffold(): void {
    const name = state.scaffoldBoard.trim();
    if (!name) {
      scaffoldResult.content = "board name is required";
      scaffoldResult.fg = C.orange;
      return;
    }

    try {
      const result = scaffoldPort(
        state.scaffoldArch,
        name,
        state.scaffoldAddress.trim() || "0x20000000",
      );
      const directory = relative(REPO_ROOT, result.directory);
      scaffoldResult.content = [
        `Wrote ${result.files.length} file(s) in ${directory}:`,
        ...result.files.map((file) => `  ${relative(REPO_ROOT, file)}`),
        "",
        "Next: fill in the TODO bodies, then add the sources to the GNUmakefile",
        "and a board entry to osbuild/src/registry.ts.",
      ].join("\n");
      scaffoldResult.fg = C.green;
      state.message = "port scaffolded";
    } catch (error) {
      scaffoldResult.content =
        error instanceof Error ? error.message : String(error);
      scaffoldResult.fg = C.orange;
    }
    updateStatus();
  }

  // ----------------------------------------------------------------------- //
  // View and focus
  // ----------------------------------------------------------------------- //

  function setView(view: View): void {
    state.view = view;
    boardsScroll.visible = view === "boards";
    portsScroll.visible = view === "ports";
    environmentScroll.visible = view === "environment";
    scaffoldScroll.visible = view === "scaffold";
    sidebar.visible = view === "boards";
    contentArea.title = ` ${VIEW_TITLES[view]} `;
    tabs.setSelectedIndex(VIEWS.indexOf(view));

    if (view === "boards") {
      renderer.currentFocusedRenderable?.blur();
      renderBoards();
    } else if (view === "ports") {
      renderer.currentFocusedRenderable?.blur();
      renderPorts();
    } else if (view === "environment") {
      renderer.currentFocusedRenderable?.blur();
      renderEnvironment();
    } else {
      scaffoldBoardInput.focus();
    }
    updateStatus();
  }

  function cycleView(delta: number): void {
    const index = VIEWS.indexOf(state.view);
    setView(VIEWS[(index + delta + VIEWS.length) % VIEWS.length]);
  }

  function moveBoard(delta: number): void {
    if (state.view !== "boards") return;
    if (delta < 0) boardSelect.moveUp();
    else boardSelect.moveDown();
  }

  const scaffoldFocusOrder: Renderable[] = [
    scaffoldArchSelect,
    scaffoldBoardInput,
    scaffoldAddressInput,
  ];

  function cycleScaffoldFocus(backwards: boolean): void {
    const current = renderer.currentFocusedRenderable;
    let index = scaffoldFocusOrder.indexOf(current as Renderable);
    if (index < 0) index = 0;
    else {
      index =
        (index + (backwards ? -1 : 1) + scaffoldFocusOrder.length) %
        scaffoldFocusOrder.length;
    }
    scaffoldFocusOrder[index]?.focus();
  }

  function quit(): void {
    renderer.destroy();
  }

  // ----------------------------------------------------------------------- //
  // Input
  // ----------------------------------------------------------------------- //

  boardSelect.on(SelectRenderableEvents.SELECTION_CHANGED, (index: number) => {
    state.boardIndex = index;
    if (state.view === "boards") renderBoards();
  });

  const onKeyPress = (key: KeyEvent): void => {
    const name = key.name.toLowerCase();
    const shifted = key.shift || key.name !== name;

    if (state.view === "scaffold") {
      if (name === "escape") {
        key.preventDefault();
        setView("boards");
      } else if (key.ctrl && name === "s") {
        key.preventDefault();
        submitScaffold();
      } else if (name === "tab") {
        key.preventDefault();
        cycleScaffoldFocus(key.shift);
      }
      return;
    }

    if (key.ctrl && name === "c") {
      key.preventDefault();
      quit();
      return;
    }
    if (name === "q") {
      key.preventDefault();
      quit();
      return;
    }

    if (name === "1") {
      key.preventDefault();
      setView("boards");
    } else if (name === "2") {
      key.preventDefault();
      setView("ports");
    } else if (name === "3") {
      key.preventDefault();
      setView("environment");
    } else if (name === "4") {
      key.preventDefault();
      setView("scaffold");
    } else if (name === "[") {
      key.preventDefault();
      cycleView(-1);
    } else if (name === "]") {
      key.preventDefault();
      cycleView(1);
    } else if (name === "up" || name === "k") {
      key.preventDefault();
      moveBoard(-1);
    } else if (name === "down" || name === "j") {
      key.preventDefault();
      moveBoard(1);
    } else if (name === "b") {
      key.preventDefault();
      runAction("build");
    } else if (name === "r") {
      key.preventDefault();
      runAction("run");
    } else if (name === "t") {
      key.preventDefault();
      runAction("test");
    } else if (name === "c" && !shifted) {
      key.preventDefault();
      runAction("clean");
    } else if (name === "c" && shifted) {
      key.preventDefault();
      clearOutput();
    } else if (name === "x") {
      key.preventDefault();
      cancelRun();
    } else if (name === "pageup") {
      key.preventDefault();
      outputScroll.scrollBy(-8);
    } else if (name === "pagedown") {
      key.preventDefault();
      outputScroll.scrollBy(8);
    } else if (name === "g" && shifted) {
      key.preventDefault();
      outputScroll.scrollTo(0);
    } else if (name === "g") {
      key.preventDefault();
      outputScroll.scrollTo(outputScroll.scrollHeight);
    }
  };

  renderer.keyInput.on("keypress", onKeyPress);

  const onDestroy = (): void => {
    state.activeRun?.kill();
    state.activeRun = null;
    renderer.keyInput.off("keypress", onKeyPress);
  };
  renderer.once("destroy", onDestroy);

  // ----------------------------------------------------------------------- //
  // Boot
  // ----------------------------------------------------------------------- //

  boardSelect.setSelectedIndex(state.boardIndex);
  renderBoards();
  renderPorts();
  renderEnvironment();
  updateOutput();
  updateStatus();
  setView("boards");

  return {
    setView,
    render: () => {
      if (state.view === "boards") renderBoards();
      else if (state.view === "ports") renderPorts();
      else if (state.view === "environment") renderEnvironment();
      updateStatus();
    },
    destroy: () => {
      state.activeRun?.kill();
      state.activeRun = null;
      renderer.keyInput.off("keypress", onKeyPress);
      renderer.off("destroy", onDestroy);
    },
  };
}

export interface TuiOptions extends BuildAppOptions {
  /** Renderer factory override, used by tests. */
  renderer?: CliRenderer;
}

/** Runs the TUI until the user quits. */
export async function runTui(options: TuiOptions = {}): Promise<void> {
  const renderer =
    options.renderer ??
    (await createCliRenderer({
      exitOnCtrlC: true,
      backgroundColor: C.bg,
      targetFps: 30,
    }));

  const app = createBuildApp(renderer, options);
  await new Promise<void>((resolve) => {
    renderer.once("destroy", () => {
      app.destroy();
      resolve();
    });
  });
}
