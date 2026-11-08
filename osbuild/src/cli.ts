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
import { join } from "node:path";

import { listBoards, listPorts, REPO_ROOT, scaffoldDriver } from "./registry";
import { runTui } from "./tui";
import { uploadSparseImage } from "./upload";

const HELP = `GothRTOS System Builder

Usage:
  osbuild [options]

Options:
  -l, --list          list boards and exit
      --list-ports    list discovered ports and exit
  --create-driver <name>
                  write a kernel driver skeleton (requires --compatible)
  --compatible <id>
                  driver compatible string; may be repeated
  --output-dir <path>
                  driver output directory (default: drivers/)
  --upload <port> send a SparseKernel v3 image to a SYSGOTH UART device
  --image <path>  image path used with --upload
  -j, --jobs <n>  parallel make jobs (default: CPU count)
  --extra <args>  extra arguments passed to make
  -h, --help          show this help

UART upload streams raw image bytes at 115200 baud. The device must already
be running SYSGOTH and the image must match the connected hardware; there is
no handshake or device-side confirmation.
`;

async function main(): Promise<number> {
  let values: {
    list?: boolean;
    "list-ports"?: boolean;
    jobs?: string;
    extra?: string;
    "create-driver"?: string;
    compatible?: string[];
    "output-dir"?: string;
    upload?: string;
    image?: string;
    help?: boolean;
  };

  try {
    ({ values } = parseArgs({
      options: {
        list: { type: "boolean", short: "l" },
        "list-ports": { type: "boolean" },
        "create-driver": { type: "string" },
        compatible: { type: "string", multiple: true },
        "output-dir": { type: "string" },
        upload: { type: "string" },
        image: { type: "string" },
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

  if (values["create-driver"] !== undefined) {
    if (values.upload !== undefined || values.image !== undefined) {
      console.error("--create-driver and --upload cannot be combined");
      return 2;
    }
    if (!values.compatible?.length) {
      console.error("--create-driver requires at least one --compatible value");
      return 2;
    }
    try {
      const directory = values["output-dir"] ?? join(REPO_ROOT, "drivers");
      const result = scaffoldDriver(
        values["create-driver"],
        values.compatible,
        directory,
      );
      console.log(`Created ${result.file}`);
      console.log("Implement the probe callback, then register the driver.");
      return 0;
    } catch (error) {
      console.error(error instanceof Error ? error.message : String(error));
      return 1;
    }
  }

  if (values.upload !== undefined || values.image !== undefined) {
    if (!values.upload || !values.image) {
      console.error("--upload and --image must be specified together");
      return 2;
    }
    try {
      const sent = await uploadSparseImage(
        values.upload,
        values.image,
        (sentBytes, total) => {
          process.stderr.write(`\rTransmitted ${sentBytes}/${total} bytes`);
        },
      );
      console.error(
        `\nTransmitted ${sent} bytes; device acceptance is unconfirmed.`,
      );
      return 0;
    } catch (error) {
      console.error(
        `\nUART upload failed: ${error instanceof Error ? error.message : String(error)}`,
      );
      return 1;
    }
  }

  if (values.compatible || values["output-dir"]) {
    console.error("--compatible and --output-dir require --create-driver");
    return 2;
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
