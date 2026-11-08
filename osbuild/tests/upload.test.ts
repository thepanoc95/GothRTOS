import { describe, expect, test } from "bun:test";

import {
  crc32,
  streamSparseImage,
  uploadSparseImage,
  validateSparseImage,
} from "../src/upload";

function makeImage(payload = new Uint8Array([0x12, 0x34, 0x56])): Uint8Array {
  const image = new Uint8Array(64 + 24 + payload.length);
  const view = new DataView(image.buffer);
  image.set([0x53, 0x50, 0x52, 0x53, 0x4b, 0x33, 0, 0]);
  view.setUint16(8, 3, true);
  view.setUint16(10, 64, true);
  view.setUint32(16, 0, true);
  view.setUint32(20, 1, true);
  view.setBigUint64(24, 0x20000000n, true);
  view.setBigUint64(32, BigInt(image.length), true);
  view.setBigUint64(40, 4096n, true);
  view.setUint32(48, crc32(payload), true);
  view.setUint32(64, 1, true);
  view.setUint32(68, payload.length, true);
  view.setBigUint64(72, 0x20000000n, true);
  view.setUint32(80, crc32(payload), true);
  image.set(payload, 88);
  view.setUint32(52, crc32(image.subarray(0, 52)), true);
  return image;
}

describe("SparseKernel UART upload", () => {
  test("validates the v3 header and data chunk", () => {
    const info = validateSparseImage(makeImage());
    expect(info).toEqual({
      arch: 0,
      chunkCount: 1,
      entryAddress: 0x20000000n,
      memorySize: 4096n,
      size: 91,
    });
  });

  test("rejects bad header and chunk checksums before streaming", async () => {
    const headerCorrupt = makeImage();
    headerCorrupt[16] = 0xff;
    expect(() => validateSparseImage(headerCorrupt)).toThrow("header CRC32");

    const chunkCorrupt = makeImage();
    chunkCorrupt[90] ^= 0xff;
    expect(() => validateSparseImage(chunkCorrupt)).toThrow("chunk 0 CRC32");

    let writes = 0;
    await expect(
      streamSparseImage(chunkCorrupt, async () => {
        writes++;
      }),
    ).rejects.toThrow("chunk 0 CRC32");
    expect(writes).toBe(0);
  });

  test("streams exact raw bytes in order and reports progress", async () => {
    const image = makeImage();
    const chunks: number[] = [];
    const progress: Array<[number, number]> = [];
    const sent = await streamSparseImage(
      image,
      async (chunk) => {
        chunks.push(...chunk);
      },
      (written, total) => progress.push([written, total]),
    );
    expect(sent).toBe(image.length);
    expect(chunks).toEqual([...image]);
    expect(progress.at(-1)).toEqual([image.length, image.length]);
  });

  test("validates before opening the serial transport", async () => {
    let configured = false;
    let opened = false;
    await expect(
      uploadSparseImage(
        "fake-port",
        "fake-image",
        undefined,
        {
          readImage: async () => {
            const image = makeImage();
            image[90] ^= 1;
            return image;
          },
          configurePort: () => {
            configured = true;
          },
          openPort: async () => {
            opened = true;
            return { write: async () => {}, close: async () => {} };
          },
        },
      ),
    ).rejects.toThrow("chunk 0 CRC32");
    expect(configured).toBe(false);
    expect(opened).toBe(false);
  });

  test("uploads through an injected serial transport", async () => {
    const image = makeImage();
    const received: number[] = [];
    let configuredPort = "";
    let closed = false;
    const sent = await uploadSparseImage(
      "/dev/fake",
      "/tmp/fake.img3",
      undefined,
      {
        readImage: async () => image,
        configurePort: (port) => {
          configuredPort = port;
        },
        openPort: async () => ({
          write: async (chunk) => {
            received.push(...chunk);
          },
          close: async () => {
            closed = true;
          },
        }),
      },
    );
    expect(sent).toBe(image.length);
    expect(configuredPort).toBe("/dev/fake");
    expect(received).toEqual([...image]);
    expect(closed).toBe(true);
  });
});
