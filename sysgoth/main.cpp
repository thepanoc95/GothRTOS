#include <stdio.h>

#if defined(_TARGET_PICO_SDK_)
#include "pico/stdlib.h"
#endif

// #include "version.h"
#define loader_version "0.0.1-alpha"

int main() {
   setup_default_uart();
   printf("SYSGOTH version ", loader_version, "\n");
   printf("This is a testboot!\n");
   return 0;
}
