// sysgoth-flash: host-side companion to the SYSGOTH bootloader.
//
// Streams a SparseKernel.img3 to a serial device so an embedded board running
// SYSGOTH can load it without being reflashed. This is a plain POSIX program;
// it is not part of the Pico firmware build (that links sysgoth.c and
// target_pico.c instead).
//
//   sysgoth-flash /dev/ttyACM0 build/pico2/SparseKernel.img3

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace {

constexpr int kBaud = 115200;

bool open_serial(const char *path, int *out_fd)
{
    int fd = ::open(path, O_WRONLY | O_NOCTTY);
    if (fd < 0) {
        std::fprintf(stderr, "sysgoth-flash: cannot open %s: %s\n", path,
                     std::strerror(errno));
        return false;
    }
    *out_fd = fd;
    return true;
}

bool stream_file(int fd, const char *path)
{
    std::FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "sysgoth-flash: cannot read %s: %s\n", path,
                     std::strerror(errno));
        return false;
    }

    unsigned char buffer[4096];
    std::size_t total = 0;
    std::size_t read_size;
    while ((read_size = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        std::size_t written = 0;
        while (written < read_size) {
            ssize_t chunk = ::write(fd, buffer + written, read_size - written);
            if (chunk < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::fprintf(stderr, "sysgoth-flash: write failed: %s\n",
                             std::strerror(errno));
                std::fclose(file);
                return false;
            }
            written += static_cast<std::size_t>(chunk);
        }
        total += read_size;
    }
    std::fclose(file);
    std::fprintf(stderr, "sysgoth-flash: sent %zu bytes to the bootloader\n",
                 total);
    return true;
}

}  // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::fprintf(stderr,
                     "usage: %s <serial-device> <SparseKernel.img3>\n", argv[0]);
        std::fprintf(stderr,
                     "Sends the image at %d baud to a board running SYSGOTH.\n",
                     kBaud);
        return 1;
    }

    int fd = -1;
    if (!open_serial(argv[1], &fd)) {
        return 1;
    }
    bool ok = stream_file(fd, argv[2]);
    ::close(fd);
    return ok ? 0 : 1;
}
