/**
 * Streaming process helper for the GothRTOS build system TUI.
 *
 * `make` is spawned without a shell so extra arguments stay separate argv
 * entries. stdout and stderr are decoded line by line and handed to the UI as
 * plain text; ANSI colour sequences are stripped because the TUI renders its
 * own styled text.
 */

import type { Command } from "./runner";

const ANSI_ESCAPE = /\x1b\[[0-?]*[ -/]*[@-~]/g;

export function stripAnsi(value: string): string {
  return value.replace(ANSI_ESCAPE, "");
}

/** Reads a byte stream and calls `onLine` for each complete line. */
export async function pumpLines(
  stream: ReadableStream<Uint8Array>,
  onLine: (line: string) => void,
): Promise<void> {
  const decoder = new TextDecoder();
  const reader = stream.getReader();
  let buffer = "";

  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let newline = buffer.indexOf("\n");
      while (newline >= 0) {
        onLine(stripAnsi(buffer.slice(0, newline).replace(/\r$/, "")));
        buffer = buffer.slice(newline + 1);
        newline = buffer.indexOf("\n");
      }
    }
  } finally {
    reader.releaseLock();
  }

  buffer += decoder.decode();
  if (buffer.length > 0) onLine(stripAnsi(buffer.replace(/\r$/, "")));
}

export interface RunCallbacks {
  onLine: (line: string) => void;
  onDone: (code: number | null) => void;
  onError?: (error: unknown) => void;
}

export interface RunHandle {
  command: string;
  kill: () => void;
  exited: Promise<number | null>;
}

/** Starts one command and streams its output. */
export function startRun(command: Command, callbacks: RunCallbacks): RunHandle {
  const child = Bun.spawn(command.argv, {
    cwd: command.cwd,
    stdin: "ignore",
    stdout: "pipe",
    stderr: "pipe",
    env: { ...process.env, TERM: "dumb", NO_COLOR: "1" },
  });

  const exited = (async (): Promise<number | null> => {
    try {
      await Promise.all([
        pumpLines(child.stdout as ReadableStream<Uint8Array>, callbacks.onLine),
        pumpLines(child.stderr as ReadableStream<Uint8Array>, callbacks.onLine),
      ]);
      const code = await child.exited;
      callbacks.onDone(code);
      return code;
    } catch (error) {
      callbacks.onError?.(error);
      callbacks.onDone(null);
      return null;
    }
  })();

  return {
    command: command.argv.join(" "),
    kill: () => {
      try {
        child.kill();
      } catch {
        // The process may already have exited.
      }
    },
    exited,
  };
}
