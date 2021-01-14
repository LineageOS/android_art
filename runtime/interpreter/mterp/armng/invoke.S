%def op_invoke_custom():
   EXPORT_PC
   FETCH r0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustom

%def op_invoke_custom_range():
   EXPORT_PC
   FETCH r0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustomRange

%def invoke_direct_or_super(helper="", range="", is_super=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
1:
   // Load the first argument (the 'this' pointer).
   FETCH r1, 2
   .if !$range
   and r1, r1, #0xf
   .endif
   GET_VREG r1, r1
   cmp r1, #0
   beq common_errNullObject    // bail if null
   b $helper
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_method
   .if $is_super
   b 1b
   .else
   tst r0, #1
   beq 1b
   and r0, r0, #-2 // Remove the extra bit that marks it's a String.<init> method.
   .if $range
   b NterpHandleStringInitRange
   .else
   b NterpHandleStringInit
   .endif
   .endif

%def op_invoke_direct():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0", is_super="0")

%def op_invoke_direct_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1", is_super="0")

%def op_invoke_super():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0", is_super="1")

%def op_invoke_super_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1", is_super="1")

%def op_invoke_polymorphic():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   FETCH r1, 2
   and r1, r1, #0xf
   GET_VREG r1, r1
   cmp r1, #0
   beq common_errNullObject    // bail if null
   b NterpCommonInvokePolymorphic

%def op_invoke_polymorphic_range():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   FETCH r1, 2
   GET_VREG r1, r1
   cmp r1, #0
   beq common_errNullObject    // bail if null
   b NterpCommonInvokePolymorphicRange

%def invoke_interface(range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
1:
   // First argument is the 'this' pointer.
   FETCH r1, 2
   .if !$range
   and r1, r1, #0xf
   .endif
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r2, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   ldr r2, [r2, #MIRROR_CLASS_IMT_PTR_OFFSET_32]
   ldr r0, [r2, r0, uxtw #2]
   .if $range
   b NterpCommonInvokeInterfaceRange
   .else
   b NterpCommonInvokeInterface
   .endif
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_method
   // For j.l.Object interface calls, the high bit is set. Also the method index is 16bits.
   cmp r0, #0
   bge 1b
   ubfx r0, r0, #0, #16
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
   FETCH_FROM_THREAD_CACHE r0, 1f
   b $helper
1:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_method
   b $helper

%def op_invoke_static():
%  invoke_static(helper="NterpCommonInvokeStatic")

%def op_invoke_static_range():
%  invoke_static(helper="NterpCommonInvokeStaticRange")

%def invoke_virtual(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the vtable offset from thread-local cache.
   FETCH_FROM_THREAD_CACHE r2, 2f
1:
   FETCH r1, 2
   .if !$range
   and r1, r1, #0xf
   .endif
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r0, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   add r0, r0, #MIRROR_CLASS_VTABLE_OFFSET_32
   ldr r0, [r0, r2, uxtw #2]
   b $helper
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_method
   mov r2, r0
   b 1b

%def op_invoke_virtual():
%  invoke_virtual(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_virtual_range():
%  invoke_virtual(helper="NterpCommonInvokeInstanceRange", range="1")

%def invoke_virtual_quick(helper="", range=""):
   EXPORT_PC
   FETCH r2, 1  // offset
   // First argument is the 'this' pointer.
   FETCH r1, 2 // arguments
   .if !$range
   and r1, r1, #0xf
   .endif
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r0, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   add r0, r0, #MIRROR_CLASS_VTABLE_OFFSET_32
   ldr r0, [r0, r2, uxtw #2]
   b $helper

%def op_invoke_virtual_quick():
%  invoke_virtual_quick(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_virtual_range_quick():
%  invoke_virtual_quick(helper="NterpCommonInvokeInstanceRange", range="1")
