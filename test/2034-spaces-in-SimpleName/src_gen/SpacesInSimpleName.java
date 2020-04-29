/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.io.*;

import org.objectweb.asm.*;

public class SpacesInSimpleName {
  public static void main(String args[]) throws Exception {
    String methodName = "method_with_spaces_"
        + "20 "
        + "a0\u00a0"
        + "1680\u1680"
        + "2000\u2000"
        + "2001\u2001"
        + "2002\u2002"
        + "2003\u2003"
        + "2004\u2004"
        + "2005\u2005"
        + "2006\u2006"
        + "2007\u2007"
        + "2008\u2008"
        + "2009\u2009"
        + "200a\u200a"
        + "202f\u202f"
        + "205f\u205f"
        + "3000\u3000";

    ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_MAXS);

    cw.visit(Opcodes.V1_8, Opcodes.ACC_PUBLIC, "Main",
      null, "java/lang/Object", null);

    MethodVisitor mvMain = cw.visitMethod(Opcodes.ACC_PUBLIC + Opcodes.ACC_STATIC,
      "main", "([Ljava/lang/String;)V", null, null);
    mvMain.visitCode();
    mvMain.visitFieldInsn(Opcodes.GETSTATIC, "java/lang/System", "out",
      "Ljava/io/PrintStream;");
    mvMain.visitLdcInsn("Hello, world!");
    mvMain.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "java/io/PrintStream",
      "println", "(Ljava/lang/String;)V", false);
    mvMain.visitMethodInsn(Opcodes.INVOKESTATIC, "Main", methodName, "()V", false);
    mvMain.visitInsn(Opcodes.RETURN);
    mvMain.visitMaxs(0, 0); // args are ignored with COMPUTE_MAXS
    mvMain.visitEnd();
    MethodVisitor mvSpaces = cw.visitMethod(Opcodes.ACC_PUBLIC + Opcodes.ACC_STATIC,
      methodName, "()V", null, null);
    mvSpaces.visitCode();
    mvSpaces.visitInsn(Opcodes.RETURN);
    mvSpaces.visitMaxs(0, 0); // args are ignored with COMPUTE_MAXS
    mvSpaces.visitEnd();

    cw.visitEnd();

    byte[] b = cw.toByteArray();
    OutputStream out = new FileOutputStream("Main.class");
    out.write(b, 0, b.length);
    out.close();
  }
}
