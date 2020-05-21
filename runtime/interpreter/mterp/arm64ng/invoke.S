%def op_invoke_custom():
   EXPORT_PC
   FETCH w0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustom

%def op_invoke_custom_range():
   EXPORT_PC
   FETCH w0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustomRange

%def invoke_direct_or_super(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
1:
   // Load the first argument (the 'this' pointer).
   FETCH w1, 2
   .if !$range
   and w1, w1, #0xf
   .endif
   GET_VREG w1, w1
   cbz w1, common_errNullObject    // bail if null
   b $helper
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_method
   tbz x0, #0, 1b
   and x0, x0, #-2 // Remove the extra bit that marks it's a String.<init> method.
   .if $range
   b NterpHandleStringInitRange
   .else
   b NterpHandleStringInit
   .endif

%def op_invoke_direct():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_direct_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1")

%def op_invoke_super():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_super_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1")

%def op_invoke_polymorphic():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   FETCH w1, 2
   and w1, w1, #0xf
   GET_VREG w1, w1
   cbz w1, common_errNullObject    // bail if null
   b NterpCommonInvokePolymorphic

%def op_invoke_polymorphic_range():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   FETCH w1, 2
   GET_VREG w1, w1
   cbz w1, common_errNullObject    // bail if null
   b NterpCommonInvokePolymorphicRange

%def invoke_interface(range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
1:
   // First argument is the 'this' pointer.
   FETCH w1, 2
   .if !$range
   and w1, w1, #0xf
   .endif
   GET_VREG w1, w1
   // Note: if w1 is null, this will be handled by our SIGSEGV handler.
   ldr w2, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   ldr x2, [x2, #MIRROR_CLASS_IMT_PTR_OFFSET_64]
   ldr x0, [x2, w0, uxtw #3]
   .if $range
   b NterpCommonInvokeInterfaceRange
   .else
   b NterpCommonInvokeInterface
   .endif
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_method
   // For j.l.Object interface calls, the high bit is set. Also the method index is 16bits.
   tbz w0, #31, 1b
   and w0, w0, #0xffff
   .if $range
   b NterpHandleInvokeInterfaceOnObjectMethodRange
   .else
   b NterpHandleInvokeInterfaceOnObjectMethod
   .endif

%def op_invoke_interface():
%  invoke_interface(range="0")

%def op_invoke_interface_range():
%  invoke_interface(range="1")

%def invoke_static(helper=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 1f
   b $helper
1:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_method
   b $helper

%def op_invoke_static():
%  invoke_static(helper="NterpCommonInvokeStatic")

%def op_invoke_static_range():
%  invoke_static(helper="NterpCommonInvokeStaticRange")

%def invoke_virtual(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE x2, 2f
1:
   FETCH w1, 2
   .if !$range
   and w1, w1, #0xf
   .endif
   GET_VREG w1, w1
   // Note: if w1 is null, this will be handled by our SIGSEGV handler.
   ldr w0, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   add w0, w0, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x0, w2, uxtw #3]
   b $helper
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_method
   mov x2, x0
   b 1b

%def op_invoke_virtual():
%  invoke_virtual(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_virtual_range():
%  invoke_virtual(helper="NterpCommonInvokeInstanceRange", range="1")

%def invoke_virtual_quick(helper="", range=""):
   EXPORT_PC
   FETCH w2, 1  // offset
   // First argument is the 'this' pointer.
   FETCH w1, 2 // arguments
   .if !$range
   and w1, w1, #0xf
   .endif
   GET_VREG w1, w1
   // Note: if w1 is null, this will be handled by our SIGSEGV handler.
   ldr w0, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   add w0, w0, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x0, w2, uxtw #3]
   b $helper

%def op_invoke_virtual_quick():
%  invoke_virtual_quick(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_virtual_range_quick():
%  invoke_virtual_quick(helper="NterpCommonInvokeInstanceRange", range="1")
