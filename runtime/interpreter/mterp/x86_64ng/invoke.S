%def invoke(helper="NterpUnimplemented"):
    call    SYMBOL($helper)

%def op_invoke_custom():
   EXPORT_PC
   movzwl 2(rPC), %edi // call_site index, first argument of runtime call.
   jmp NterpCommonInvokeCustom

%def op_invoke_custom_range():
   EXPORT_PC
   movzwl 2(rPC), %edi // call_site index, first argument of runtime call.
   jmp NterpCommonInvokeCustomRange

%def invoke_direct_or_super(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rdi, 2f
1:
   // Load the first argument (the 'this' pointer).
   movzwl 4(rPC), %r11d // arguments
   .if !$range
   andq $$0xf, %r11
   .endif
   movl (rFP, %r11, 4), %esi
   // NullPointerException check.
   movl (%esi), %eax
   jmp $helper
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_method
   movq %rax, %rdi
   testl MACRO_LITERAL(1), %eax
   je 1b
   andq $$-2, %rdi  // Remove the extra bit that marks it's a String.<init> method.
   .if $range
   jmp NterpHandleStringInitRange
   .else
   jmp NterpHandleStringInit
   .endif

%def op_invoke_direct():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_direct_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1")

%def op_invoke_polymorphic():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   movzwl 4(rPC), %r11d // arguments
   andq $$0xf, %r11
   movl (rFP, %r11, 4), %esi
   // NullPointerException check.
   movl (%esi), %eax
   jmp NterpCommonInvokePolymorphic

%def op_invoke_polymorphic_range():
   EXPORT_PC
   // No need to fetch the target method.
   // Load the first argument (the 'this' pointer).
   movzwl 4(rPC), %r11d // arguments
   movl (rFP, %r11, 4), %esi
   // NullPointerException check.
   movl (%esi), %eax
   jmp NterpCommonInvokePolymorphicRange

%def invoke_interface(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   .if !$range
   andq $$0xf, %r11
   .endif
   movl (rFP, %r11, 4), %esi
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_IMT_PTR_OFFSET_64(%edx), %rdx
   movq (%rdx, %rax, 8), %rdi
   jmp $helper
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_method
   testl %eax, %eax
   jns 1b
   // For j.l.Object interface calls, the high bit is set. Also the method index is 16bits.
   andl LITERAL(0xffff), %eax
   .if $range
   jmp NterpHandleInvokeInterfaceOnObjectMethodRange
   .else
   jmp NterpHandleInvokeInterfaceOnObjectMethod
   .endif

%def op_invoke_interface():
%  invoke_interface(helper="NterpCommonInvokeInterface", range="0")

%def op_invoke_interface_range():
%  invoke_interface(helper="NterpCommonInvokeInterfaceRange", range="1")

%def invoke_static(helper=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rdi, 1f
   jmp $helper
1:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_method
   movq %rax, %rdi
   jmp $helper

%def op_invoke_static():
%  invoke_static(helper="NterpCommonInvokeStatic")

%def op_invoke_static_range():
%  invoke_static(helper="NterpCommonInvokeStaticRange")

%def op_invoke_super():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_super_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range="1")

%def invoke_virtual(helper="", range=""):
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rdi, 2f
1:
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   .if !$range
   andq $$0xf, %r11
   .endif
   movl (rFP, %r11, 4), %esi
   // Note: if esi is null, this will be handled by our SIGSEGV handler.
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_VTABLE_OFFSET_64(%edx, %edi, 8), %rdi
   jmp $helper
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_method
   movl %eax, %edi
   jmp 1b

%def op_invoke_virtual():
%  invoke_virtual(helper="NterpCommonInvokeInstance", range="0")

%def op_invoke_virtual_quick():
   EXPORT_PC
   movzwl 2(rPC), %eax // offset
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   andq $$0xf, %r11
   movl (rFP, %r11, 4), %esi
   // Note: if esi is null, this will be handled by our SIGSEGV handler.
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_VTABLE_OFFSET_64(%edx, %eax, 8), %rdi
   jmp NterpCommonInvokeInstance

%def op_invoke_virtual_range():
%  invoke_virtual(helper="NterpCommonInvokeInstanceRange", range="1")

%def op_invoke_virtual_range_quick():
   EXPORT_PC
   movzwl 2(rPC), %eax // offset
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   movl (rFP, %r11, 4), %esi
   // Note: if esi is null, this will be handled by our SIGSEGV handler.
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_VTABLE_OFFSET_64(%edx, %eax, 8), %rdi
   jmp NterpCommonInvokeInstanceRange
