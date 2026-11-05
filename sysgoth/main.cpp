#include <stdio.h>
#include "pico/stdlib.h"

// #include "version.h"
#define loader_version "0.0.1-alpha"

int main() {
   setup_default_uart();
   printf("SYSGOTH version ", loader_version, "\n");
   printf("This is a testboot!\n");
   return 0;
}
