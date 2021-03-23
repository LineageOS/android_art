%def op_check_cast():
  jmp NterpCheckCast

%def op_iget_boolean():
   jmp NterpGetBooleanInstanceField

%def op_iget_byte():
   jmp NterpGetByteInstanceField

%def op_iget_char():
   jmp NterpGetCharInstanceField

%def op_iget_object():
    jmp NterpGetObjectInstanceField

%def op_iget_short():
   jmp NterpGetShortInstanceField

%def op_iget_wide():
   jmp NterpGetWideInstanceField

%def op_instance_of():
   jmp NterpInstanceOf

%def op_iget():
   jmp NterpGetInstanceField

%def op_iput():
   jmp NterpPutInstanceField

%def op_iput_boolean():
   jmp NterpPutBooleanInstanceField

%def op_iput_byte():
   jmp NterpPutByteInstanceField

%def op_iput_char():
   jmp NterpPutCharInstanceField

%def op_iput_object():
    jmp NterpPutObjectInstanceField

%def op_iput_short():
   jmp NterpPutShortInstanceField

%def op_iput_wide():
   jmp NterpPutWideInstanceField

%def op_sget(load="movl", wide="0"):
   jmp NterpGetIntStaticField

%def op_sget_boolean():
   jmp NterpGetBooleanStaticField

%def op_sget_byte():
   jmp NterpGetByteStaticField

%def op_sget_char():
   jmp NterpGetCharStaticField

%def op_sget_object():
   jmp NterpGetObjectStaticField

%def op_sget_short():
   jmp NterpGetShortStaticField

%def op_sget_wide():
   jmp NterpGetWideStaticField

%def op_sput():
   jmp NterpPutStaticField

%def op_sput_boolean():
   jmp NterpPutBooleanStaticField

%def op_sput_byte():
   jmp NterpPutByteStaticField

%def op_sput_char():
   jmp NterpPutCharStaticField

%def op_sput_object():
   jmp NterpPutObjectStaticField

%def op_sput_short():
   jmp NterpPutShortStaticField

%def op_sput_wide():
   jmp NterpPutWideStaticField

%def op_new_instance():
   // The routine is too big to fit in a handler, so jump to it.
   jmp NterpNewInstance
