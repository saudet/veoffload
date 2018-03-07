/**
 * @file api.c
 * @brief VEO API functions
 */

#include <ve_offload.h>

#include "ProcHandle.hpp"
#include "VEOException.hpp"
#include "log.hpp"

namespace veo {
namespace api {
// cast helper from C API to C++ implementation
ProcHandle *ProcHandleFromC(veo_proc_handle *h)
{
  return reinterpret_cast<ProcHandle *>(h);
}
ThreadContext *ThreadContextFromC(veo_thr_ctxt *c)
{
  return reinterpret_cast<ThreadContext *>(c);
}
} // namespace veo::api
} // namespace veo

using veo::api::ProcHandleFromC;
using veo::api::ThreadContextFromC;
using veo::VEOException;

// implementation of VEO API functions
/**
 * @brief lower level function to create a VE process
 * @param ossock path to VE OS socket
 * @param vedev path to VE device file
 * @return pointer to VEO process handle upon success; NULL upon failure.
 */
veo_proc_handle *veo_proc__create(const char *ossock, const char *vedev)
{
  try {
    auto rv = new veo::ProcHandle(ossock, vedev);
    return rv->toCHandle();
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to create ProcHandle: %s", e.what());
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief create a VE process
 * @param venode VE node number
 * @return pointer to VEO process handle upon success; NULL upon failure.
 */
veo_proc_handle *veo_proc_create(int venode)
{
  char vedev[16];// the size of "/dev/veslot" = 12.
  snprintf(vedev, sizeof(vedev), VE_DEV, venode);
  char ossock[sizeof(VEOS_SOCKET) + 16];
  snprintf(ossock, sizeof(ossock), VEOS_SOCKET, venode);
  return veo_proc__create(ossock, vedev);
}

/**
 * @brief destroy a VE process
 * @param proc pointer to VEO process handle
 * @return zero on success, negative if any exception occured
 */
int veo_proc_destroy(veo_proc_handle *proc)
{
  try {
    ProcHandleFromC(proc)->exitProc();
  } catch (VEOException &e) {
    return -1;
  }
  return 0;
}

/**
 * @brief load a VE library
 * @param proc VEO process handle
 * @param libname a library file name to load
 * @return a handle for the library upon success; zero upon failure.
 */
uint64_t veo_load_library(veo_proc_handle *proc, const char *libname)
{
  try {
    return ProcHandleFromC(proc)->loadLibrary(libname);
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to load library: %s", e.what());
    errno = e.err();
    return 0;
  }
}

/**
 * @brief find a symbol in VE program
 * @param proc VEO process handle
 * @param libhdl a library handle
 * @param symname symbol name to find
 * @return VEMVA of the symbol upon success; zero upon failure.
 */
uint64_t veo_get_sym(veo_proc_handle *proc, uint64_t libhdl,
                     const char *symname)
{
  try {
    return ProcHandleFromC(proc)->getSym(libhdl, symname);
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to get symbol: %s", e.what());
    errno = e.err();
    return 0;
  }
}

/**
 * @brief open a VEO context
 *
 * Create a new VEO context, a pseudo thread and VE thread for the context.
 *
 * @param proc VEO process handle
 * @return a pointer to VEO thread context upon success; NULL upon failure.
 */
veo_thr_ctxt *veo_context_open(veo_proc_handle *proc)
{
  try {
    return ProcHandleFromC(proc)->openContext()->toCHandle();
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to open context: %s", e.what());
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief close a VEO context
 * @param ctx a VEO context to close
 * @return zero upon success; negative upon failure.
 */
int veo_context_close(veo_thr_ctxt *ctx)
{
  auto c = ThreadContextFromC(ctx);
  if (c->isMainThread()) {
    VEO_ERROR(c, "DO NOT close the main thread %p", c);
    return -EINVAL;
  }
  int rv = c->close();
  if (rv == 0) {
    delete c;
  }
  return rv;
}

/**
 * @brief get VEO context state
 * @return the state of the VEO context state.
 */
int veo_get_context_state(veo_thr_ctxt *ctx)
{
  return ThreadContextFromC(ctx)->getState();
}

/**
 * @brief request a VE thread to call a function
 * @param ctx VEO context to execute the function on VE.
 * @param addr VEMVA of the function to call
 * @param args arguments to be passed to the function
 * @return request ID; VEO_REQUEST_ID_INVALID upon failure.
 */
uint64_t veo_call_async(veo_thr_ctxt *ctx, uint64_t addr,
                        const veo_call_args *args)
{
  try {
    return ThreadContextFromC(ctx)->callAsync(addr, *args);
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief pick up a resutl from VE function if it has finished
 * @param ctx VEO context
 * @param reqid request ID
 * @param retp pointer to buffer to store the return value from the function.
 * @return 0 on success; -1 on failure; 3 if function not finished.
 */
int veo_call_peek_result(veo_thr_ctxt *ctx, uint64_t reqid, uint64_t *retp)
{
  try {
    return ThreadContextFromC(ctx)->callPeekResult(reqid, retp);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief pick up a resutl from VE function
 * @param ctx VEO context
 * @param reqid request ID
 * @param retp pointer to buffer to store the return value from the function.
 * @return zero upon success; negative upon failure.
 */
int veo_call_wait_result(veo_thr_ctxt *ctx, uint64_t reqid, uint64_t *retp)
{
  try {
    return ThreadContextFromC(ctx)->callWaitResult(reqid, retp);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief Allocate a VE memory buffer
 * @param h VEO process handle
 * @param addr [out] VEMVA address
 * @param size [in] size in bytes
 * @return zero upon success; negative upon failure.
 */
int veo_alloc_mem(veo_proc_handle *h, uint64_t *addr, const size_t size)
{
  try {
    *addr = ProcHandleFromC(h)->allocBuff(size);
    if (*addr == 0UL)
      return -1;
  } catch (VEOException &e) {
    return -2;
  }
  return 0;
}

/**
 * @brief Free a VE memory buffer
 * @param h VEO process handle
 * @param addr [in] VEMVA address
 * @return zero upon success; negative upon failure.
 */
int veo_free_mem(veo_proc_handle *h, uint64_t addr)
{
  try {
    ProcHandleFromC(h)->freeBuff(addr);
  } catch (VEOException &e) {
    return -1;
  }
	return 0;
}

/**
 * @brief Read VE memory
 * @param h VEO process handle
 * @param dst destination VHVA
 * @param src source VEMVA
 * @param size size in byte
 * @return zero upon success; negative upon failure.
 */
int veo_read_mem(veo_proc_handle *h, void *dst, uint64_t src, size_t size)
{
  try {
    return ProcHandleFromC(h)->readMem(dst, src, size);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief Write VE memory
 * @param h VEO process handle
 * @param dst destination VEMVA
 * @param src source VHVA
 * @param size size in byte
 * @return zero upon success; negative upon failure.
 */
int veo_write_mem(veo_proc_handle *h, uint64_t dst, void *src,
                  size_t size)
{
  try {
    return ProcHandleFromC(h)->writeMem(dst, src, size);
  } catch (VEOException &e) {
    return -1;
  }
}