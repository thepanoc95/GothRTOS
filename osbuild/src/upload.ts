import { spawnSync } from "node:child_process";
import { constants } from "node:fs";
import { open, readFile } from "node:fs/promises";

const MAGIC = [0x53, 0x50, 0x52, 0x53, 0x4b, 0x33, 0x00, 0x00];
const HEADER_SIZE = 64;
const CHUNK_HEADER_SIZE = 24;
const BAUD_RATE = 115_200;
const WRITE_SIZE = 4096;

export interface SparseImageInfo {
  arch: number;
  chunkCount: number;
  entryAddress: bigint;
  memorySize: bigint;
  size: number;
}

export type SerialWrite = (chunk: Uint8Array) => Promise<void>;
export type UploadProgress = (sent: number, total: number) => void;

export interface SerialTransport {
  write: SerialWrite;
  close: () => Promise<void>;
}

export interface UploadDependencies {
  configurePort?: (portPath: string) => void;
  openPort?: (portPath: string) => Promise<SerialTransport>;
  readImage?: (imagePath: string) => Promise<Uint8Array>;
}

export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ ((crc & 1) === 1 ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/** Validates the exact SparseKernel v3 image contract consumed by SYSGOTH. */
export function validateSparseImage(bytes: Uint8Array): SparseImageInfo {
  if (bytes.length < HEADER_SIZE) {
    throw new Error("SparseKernel image is shorter than its 64-byte header");
  }
  if (!MAGIC.every((byte, index) => bytes[index] === byte)) {
    throw new Error("invalid SparseKernel image magic (expected SPRSK3)");
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const version = view.getUint16(8, true);
  const headerSize = view.getUint16(10, true);
  if (version !== 3 || headerSize !== HEADER_SIZE) {
    throw new Error(`unsupported SparseKernel image version/header: ${version}/${headerSize}`);
  }

  const totalSize = view.getBigUint64(32, true);
  if (totalSize !== BigInt(bytes.length)) {
    throw new Error(
      `SparseKernel size mismatch: header declares ${totalSize} bytes, file has ${bytes.length}`,
    );
  }
  if (crc32(bytes.subarray(0, 52)) !== view.getUint32(52, true)) {
    throw new Error("SparseKernel header CRC32 mismatch");
  }

  const chunkCount = view.getUint32(20, true);
  const maxChunkCount = Math.floor((bytes.length - HEADER_SIZE) / CHUNK_HEADER_SIZE);
  if (chunkCount > maxChunkCount) {
    throw new Error("SparseKernel chunk count exceeds the available image data");
  }

  let offset = HEADER_SIZE;
  for (let index = 0; index < chunkCount; index++) {
    if (bytes.length - offset < CHUNK_HEADER_SIZE) {
      throw new Error(`SparseKernel chunk ${index} has a truncated header`);
    }
    const type = view.getUint32(offset, true);
    const length = view.getUint32(offset + 4, true);
    const expectedCrc = view.getUint32(offset + 16, true);
    if (type !== 0 && type !== 1) {
      throw new Error(`SparseKernel chunk ${index} has unknown type ${type}`);
    }
    if (type === 0 && length === 0) {
      throw new Error(`SparseKernel zero chunk ${index} has zero length`);
    }

    offset += CHUNK_HEADER_SIZE;
    if (type === 1) {
      if (length > bytes.length - offset) {
        throw new Error(`SparseKernel data chunk ${index} is truncated`);
      }
      if (crc32(bytes.subarray(offset, offset + length)) !== expectedCrc) {
        throw new Error(`SparseKernel data chunk ${index} CRC32 mismatch`);
      }
      offset += length;
    }
  }
  if (offset !== bytes.length) {
    throw new Error("SparseKernel image has trailing or unclaimed data");
  }

  return {
    arch: view.getUint32(16, true),
    chunkCount,
    entryAddress: view.getBigUint64(24, true),
    memorySize: view.getBigUint64(40, true),
    size: bytes.length,
  };
}

/** Streams already-validated bytes exactly as the SYSGOTH raw UART protocol expects. */
export async function streamSparseImage(
  bytes: Uint8Array,
  write: SerialWrite,
  onProgress?: UploadProgress,
): Promise<number> {
  const info = validateSparseImage(bytes);
  return streamValidatedImage(bytes, info.size, write, onProgress);
}

async function streamValidatedImage(
  bytes: Uint8Array,
  size: number,
  write: SerialWrite,
  onProgress?: UploadProgress,
): Promise<number> {
  let sent = 0;
  while (sent < size) {
    const end = Math.min(sent + WRITE_SIZE, size);
    await write(bytes.subarray(sent, end));
    sent = end;
    onProgress?.(sent, size);
  }
  return sent;
}

export function configureSerialPort(
  portPath: string,
  platform: NodeJS.Platform = process.platform,
): void {
  const selector = platform === "darwin" ? "-f" : platform === "linux" ? "-F" : null;
  if (selector === null) {
    throw new Error("UART upload currently requires Linux or macOS with stty");
  }
  const result = spawnSync(
    "stty",
    [selector, portPath, String(BAUD_RATE), "raw", "-echo", "-ixon", "-ixoff", "-crtscts"],
    { encoding: "utf8" },
  );
  if (result.error) {
    throw new Error(`could not configure serial port: ${result.error.message}`);
  }
  if (result.status !== 0) {
    throw new Error(
      `stty failed for ${portPath}: ${(result.stderr || result.stdout || "unknown error").trim()}`,
    );
  }
}

async function openSerialPort(portPath: string): Promise<SerialTransport> {
  const handle = await open(portPath, constants.O_WRONLY | constants.O_NOCTTY);
  return {
    write: async (chunk) => {
      let offset = 0;
      while (offset < chunk.length) {
        const { bytesWritten } = await handle.write(
          chunk,
          offset,
          chunk.length - offset,
          null,
        );
        if (bytesWritten === 0) {
          throw new Error(`serial write to ${portPath} made no progress`);
        }
        offset += bytesWritten;
      }
    },
    close: () => handle.close(),
  };
}

/** Validates the image before opening the serial device, then streams raw bytes. */
export async function uploadSparseImage(
  portPath: string,
  imagePath: string,
  onProgress?: UploadProgress,
  dependencies: UploadDependencies = {},
): Promise<number> {
  const bytes = await (dependencies.readImage ?? readFile)(imagePath);
  const info = validateSparseImage(bytes);
  (dependencies.configurePort ?? configureSerialPort)(portPath);
  const transport = await (dependencies.openPort ?? openSerialPort)(portPath);
  try {
    return await streamValidatedImage(bytes, info.size, transport.write, onProgress);
  } finally {
    await transport.close();
  }
}
