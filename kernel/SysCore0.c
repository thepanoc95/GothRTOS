#include "hal.h"
#include "nanokernel.h"
#include "pico/stdlib.h"

int main(void) {
	OSBootStartup();
	GothKernelInit();

	for (;;) {
		if (!GothKernelDispatchOne()) {
			tight_loop_contents();
		}
	}
}
