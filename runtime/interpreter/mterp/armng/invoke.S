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
%  slow_path = add_helper(lambda: op_invoke_interface_slow_path())
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE r4, ${slow_path}
.L${opcode}_resume:
   // First argument is the 'this' pointer.
   FETCH r1, 2
   .if !$range
   and r1, r1, #0xf
   .endif
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r2, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   // Test the first two bits of the fetched ArtMethod:
   // - If the first bit is set, this is a method on j.l.Object
   // - If the second bit is set, this is a default method.
   tst r4, #3
   bne 2f
   ldrh r3, [r4, #ART_METHOD_IMT_INDEX_OFFSET]
1:
   ldr r2, [r2, #MIRROR_CLASS_IMT_PTR_OFFSET_32]
   ldr r0, [r2, r3, lsl #2]
   .if $range
   b NterpCommonInvokeInterfaceRange
   .else
   b NterpCommonInvokeInterface
   .endif
2:
   tst r4, #1
   bne 3f
   and r4, r4, #-4
   ldrh r3, [r4, #ART_METHOD_METHOD_INDEX_OFFSET]
   and r3, r3, #ART_METHOD_IMT_MASK
   b 1b
3:
   lsr r4, r4, #16
   add r2, r2, #MIRROR_CLASS_VTABLE_OFFSET_32
   ldr r0, [r2, r4, lsl #2]
   .if $range
   b NterpCommonInvokeInstanceRange
   .else
   b NterpCommonInvokeInstance
   .endif

%def op_invoke_interface_slow_path():
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_method
   mov r4, r0
   b .L${opcode}_resume

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
   ldr r0, [r0, r2, lsl #2]
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
