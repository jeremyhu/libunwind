//===--------------------- Unwind_AppleExtras.cpp -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//===----------------------------------------------------------------------===//

#include "config.h"
#include "DwarfParser.hpp"
#include "unwind_ext.h"


// private keymgr stuff
#define KEYMGR_GCC3_DW2_OBJ_LIST 302
extern "C" {
 extern void _keymgr_set_and_unlock_processwide_ptr(int key, void *ptr);
 extern void *_keymgr_get_and_lock_processwide_ptr(int key);
}

// undocumented libgcc "struct object"
struct libgcc_object {
  void          *start;
  void          *unused1;
  void          *unused2;
  void          *fde;
  unsigned long  encoding;
  void          *fde_end;
  libgcc_object *next;
};

// undocumented libgcc "struct km_object_info" referenced by
// KEYMGR_GCC3_DW2_OBJ_LIST
struct libgcc_object_info {
  libgcc_object   *seen_objects;
  libgcc_object   *unseen_objects;
  unsigned         spare[2];
};


// static linker symbols to prevent wrong two level namespace for _Unwind symbols
#define NOT_HERE_BEFORE_10_6(sym)
#define NEVER_HERE(sym)

#if _LIBUNWIND_BUILD_ZERO_COST_APIS

//
// symbols in libSystem.dylib in 10.6 and later, but are in libgcc_s.dylib in
// earlier versions
//
NOT_HERE_BEFORE_10_6(_Unwind_DeleteException)
NOT_HERE_BEFORE_10_6(_Unwind_Find_FDE)
NOT_HERE_BEFORE_10_6(_Unwind_ForcedUnwind)
NOT_HERE_BEFORE_10_6(_Unwind_GetGR)
NOT_HERE_BEFORE_10_6(_Unwind_GetIP)
NOT_HERE_BEFORE_10_6(_Unwind_GetLanguageSpecificData)
NOT_HERE_BEFORE_10_6(_Unwind_GetRegionStart)
NOT_HERE_BEFORE_10_6(_Unwind_RaiseException)
NOT_HERE_BEFORE_10_6(_Unwind_Resume)
NOT_HERE_BEFORE_10_6(_Unwind_SetGR)
NOT_HERE_BEFORE_10_6(_Unwind_SetIP)
NOT_HERE_BEFORE_10_6(_Unwind_Backtrace)
NOT_HERE_BEFORE_10_6(_Unwind_FindEnclosingFunction)
NOT_HERE_BEFORE_10_6(_Unwind_GetCFA)
NOT_HERE_BEFORE_10_6(_Unwind_GetDataRelBase)
NOT_HERE_BEFORE_10_6(_Unwind_GetTextRelBase)
NOT_HERE_BEFORE_10_6(_Unwind_Resume_or_Rethrow)
NOT_HERE_BEFORE_10_6(_Unwind_GetIPInfo)
NOT_HERE_BEFORE_10_6(__register_frame)
NOT_HERE_BEFORE_10_6(__deregister_frame)

//
// symbols in libSystem.dylib for compatibility, but we don't want any new code
// using them
//
NEVER_HERE(__register_frame_info_bases)
NEVER_HERE(__register_frame_info)
NEVER_HERE(__register_frame_info_table_bases)
NEVER_HERE(__register_frame_info_table)
NEVER_HERE(__register_frame_table)
NEVER_HERE(__deregister_frame_info)
NEVER_HERE(__deregister_frame_info_bases)

#endif // _LIBUNWIND_BUILD_ZERO_COST_APIS




#if _LIBUNWIND_BUILD_SJLJ_APIS
//
// symbols in libSystem.dylib in iOS 5.0 and later, but are in libgcc_s.dylib in
// earlier versions
//
NOT_HERE_BEFORE_5_0(_Unwind_GetLanguageSpecificData)
NOT_HERE_BEFORE_5_0(_Unwind_GetRegionStart)
NOT_HERE_BEFORE_5_0(_Unwind_GetIP)
NOT_HERE_BEFORE_5_0(_Unwind_SetGR)
NOT_HERE_BEFORE_5_0(_Unwind_SetIP)
NOT_HERE_BEFORE_5_0(_Unwind_DeleteException)
NOT_HERE_BEFORE_5_0(_Unwind_SjLj_Register)
NOT_HERE_BEFORE_5_0(_Unwind_GetGR)
NOT_HERE_BEFORE_5_0(_Unwind_GetIPInfo)
NOT_HERE_BEFORE_5_0(_Unwind_GetCFA)
NOT_HERE_BEFORE_5_0(_Unwind_SjLj_Resume)
NOT_HERE_BEFORE_5_0(_Unwind_SjLj_RaiseException)
NOT_HERE_BEFORE_5_0(_Unwind_SjLj_Resume_or_Rethrow)
NOT_HERE_BEFORE_5_0(_Unwind_SjLj_Unregister)

#endif // _LIBUNWIND_BUILD_SJLJ_APIS


namespace libunwind {

_LIBUNWIND_HIDDEN
bool checkKeyMgrRegisteredFDEs(uintptr_t pc, void *&fde) {
#if __MAC_OS_X_VERSION_MIN_REQUIRED
  // lastly check for old style keymgr registration of dynamically generated
  // FDEs acquire exclusive access to libgcc_object_info
  libgcc_object_info *head = (libgcc_object_info *)
                _keymgr_get_and_lock_processwide_ptr(KEYMGR_GCC3_DW2_OBJ_LIST);
  if (head != NULL) {
    // look at each FDE in keymgr
    for (libgcc_object *ob = head->unseen_objects; ob != NULL; ob = ob->next) {
      CFI_Parser<LocalAddressSpace>::FDE_Info fdeInfo;
      CFI_Parser<LocalAddressSpace>::CIE_Info cieInfo;
      const char *msg = CFI_Parser<LocalAddressSpace>::decodeFDE(
                                      LocalAddressSpace::sThisAddressSpace,
                                      (uintptr_t)ob->fde, &fdeInfo, &cieInfo);
      if (msg == NULL) {
        // Check if this FDE is for a function that includes the pc
        if ((fdeInfo.pcStart <= pc) && (pc < fdeInfo.pcEnd)) {
          fde = (void*)fdeInfo.pcStart;
          _keymgr_set_and_unlock_processwide_ptr(KEYMGR_GCC3_DW2_OBJ_LIST,
                                                 head);
          return true;
        }
      }
    }
  }
  // release libgcc_object_info
  _keymgr_set_and_unlock_processwide_ptr(KEYMGR_GCC3_DW2_OBJ_LIST, head);
#else
  (void)pc;
  (void)fde;
#endif
  return false;
}

}


#if !defined(FOR_DYLD) && _LIBUNWIND_BUILD_SJLJ_APIS

#ifndef _LIBUNWIND_HAS_NO_THREADS
  #include <System/pthread_machdep.h>
#else
  _Unwind_FunctionContext *fc_ = nullptr;
#endif

// Accessors to get get/set linked list of frames for sjlj based execeptions.
_LIBUNWIND_HIDDEN
struct _Unwind_FunctionContext *__Unwind_SjLj_GetTopOfFunctionStack() {
#ifndef _LIBUNWIND_HAS_NO_THREADS
  return (struct _Unwind_FunctionContext *)
    _pthread_getspecific_direct(__PTK_LIBC_DYLD_Unwind_SjLj_Key);
#else
  return fc_;
#endif
}

_LIBUNWIND_HIDDEN
void __Unwind_SjLj_SetTopOfFunctionStack(struct _Unwind_FunctionContext *fc) {
#ifndef _LIBUNWIND_HAS_NO_THREADS
  _pthread_setspecific_direct(__PTK_LIBC_DYLD_Unwind_SjLj_Key, fc);
#else
  fc_ = fc;
#endif
}
#endif
