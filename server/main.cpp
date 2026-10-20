#include <switch.h>
#include <memory>
#include "app.hpp"
#include "socket_server.hpp"
#include "ipc_service.hpp"

// Sysmodules share the small System memory pool with every other background
// module; a 4 MB heap plus default socket buffers fails to launch on a busy
// setup (kernel LimitReached 0x10801). 2 MB with small socket buffers fits.
#define INNER_HEAP_SIZE 0x200000

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;

void __libnx_initheap(void) {
  static u8 inner_heap[INNER_HEAP_SIZE];
  extern void* fake_heap_start;
  extern void* fake_heap_end;
  fake_heap_start = inner_heap;
  fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
  Result rc = smInitialize();
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  rc = setsysInitialize();
  if (R_SUCCEEDED(rc)) {
    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
      hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    setsysExit();
  }

  // Default config reserves ~2 MB of transfer memory from the heap.
  SocketInitConfig socket_config = *socketGetDefaultInitConfig();
  socket_config.tcp_tx_buf_size     = 0x4000;
  socket_config.tcp_rx_buf_size     = 0x4000;
  socket_config.tcp_tx_buf_max_size = 0x20000;
  socket_config.tcp_rx_buf_max_size = 0x20000;
  socket_config.udp_tx_buf_size     = 0;
  socket_config.udp_rx_buf_size     = 0;
  socket_config.sb_efficiency       = 1;
  rc = socketInitialize(&socket_config);
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

  rc = fsInitialize();
  if (R_FAILED(rc)) {
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
  }
  fsdevMountSdmc();
}

void __appExit(void) {
  fsdevUnmountAll();
  fsExit();
  socketExit();
  smExit();
}

#ifdef __cplusplus
}
#endif

int main(int, char*[]) {
  ipc::StartIpcThread();
  http::App app(std::make_unique<http::SocketServer>());
  app.Run(57331);
  return 0;
}
