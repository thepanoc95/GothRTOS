#!/usr/bin/env bun
/**
 * GothRTOS System Builder — command line front end.
 *
 *     bun run osbuild/src/cli.ts              # start the TUI
 *     bun run osbuild/src/cli.ts --list       # list boards, ports and devices
 *     bun run osbuild/src/cli.ts --list-ports # list ports found under pexpert/
 *
 * Or use `make build-ui`.
 */

import { parseArgs } from "node:util";

import { listBoards, listPorts } from "./registry";
import { runTui } from "./tui";

const HELP = `GothRTOS System Builder

Usage:
  osbuild [options]

Options:
  -l, --list          list boards and exit
      --list-ports    list discovered ports and exit
  -j, --jobs <n>      parallel make jobs (default: CPU count)
      --extra <args>  extra arguments passed to make
  -h, --help          show this help
`;

async function main(): Promise<number> {
  let values: {
    list?: boolean;
    "list-ports"?: boolean;
    jobs?: string;
    extra?: string;
    help?: boolean;
  };

  try {
    ({ values } = parseArgs({
      options: {
        list: { type: "boolean", short: "l" },
        "list-ports": { type: "boolean" },
        jobs: { type: "string", short: "j" },
        extra: { type: "string" },
        help: { type: "boolean", short: "h" },
      },
      strict: true,
    }));
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    console.error(HELP);
    return 2;
  }

  if (values.help) {
    console.log(HELP);
    return 0;
  }
  if (values.list) {
    listBoards();
    return 0;
  }
  if (values["list-ports"]) {
    listPorts();
    return 0;
  }

  let jobs: number | undefined;
  if (values.jobs !== undefined) {
    jobs = Number(values.jobs);
    if (!Number.isInteger(jobs) || jobs < 1 || jobs > 64) {
      console.error(`invalid job count: ${values.jobs}`);
      return 2;
    }
  }

  await runTui({ jobs, extraArgs: values.extra });
  return 0;
}

if (import.meta.main) {
  process.exit(await main());
}
