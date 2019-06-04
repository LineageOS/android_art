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
 * limitations under the License
 */

package com.android.class2greylist;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Class which can parse either dex style signatures (e.g. Lfoo/bar/baz$bat;->foo()V) or javadoc
 * links to class members (e.g. {@link #toString()} or {@link java.util.List#clear()}).
 */
public class ApiComponents {
    private static final String PRIMITIVE_TYPES = "ZBCSIJFD";
    private final PackageAndClassName mPackageAndClassName;
    // The reference can be just to a class, in which case mMemberName should be empty.
    private final String mMemberName;
    // If the member being referenced is a field, this will always be empty.
    private final String mMethodParameterTypes;

    private ApiComponents(PackageAndClassName packageAndClassName, String memberName,
            String methodParameterTypes) {
        mPackageAndClassName = packageAndClassName;
        mMemberName = memberName;
        mMethodParameterTypes = methodParameterTypes;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder()
                .append(mPackageAndClassName.packageName)
                .append(".")
                .append(mPackageAndClassName.className);
        if (!mMemberName.isEmpty()) {
            sb.append("#").append(mMemberName).append("(").append(mMethodParameterTypes).append(
                    ")");
        }
        return sb.toString();
    }

    public PackageAndClassName getPackageAndClassName() {
        return mPackageAndClassName;
    }

    public String getMemberName() {
        return mMemberName;
    }

    public String getMethodParameterTypes() {
        return mMethodParameterTypes;
    }

    /**
     * Parse a JNI class descriptor. e.g. Lfoo/bar/Baz;
     *
     * @param sc Cursor over string assumed to contain a JNI class descriptor.
     * @return The fully qualified class, in 'dot notation' (e.g. foo.bar.Baz for a class named Baz
     * in the foo.bar package). The cursor will be placed after the semicolon.
     */
    private static String parseJNIClassDescriptor(StringCursor sc)
            throws SignatureSyntaxError, StringCursorOutOfBoundsException {
        if (sc.peek() != 'L') {
            throw new SignatureSyntaxError(
                    "Expected JNI class descriptor to start with L, but instead got " + sc.peek(),
                    sc);
        }
        // Consume the L.
        sc.next();
        int semiColonPos = sc.find(';');
        if (semiColonPos == -1) {
            throw new SignatureSyntaxError("Expected semicolon at the end of JNI class descriptor",
                    sc);
        }
        String jniClassDescriptor = sc.next(semiColonPos);
        // Consume the semicolon.
        sc.next();
        return jniClassDescriptor.replace("/", ".");
    }

    /**
     * Parse a primitive JNI type
     *
     * @param sc Cursor over a string assumed to contain a primitive JNI type.
     * @return String containing parsed primitive JNI type.
     */
    private static String parseJNIPrimitiveType(StringCursor sc)
            throws SignatureSyntaxError, StringCursorOutOfBoundsException {
        char c = sc.next();
        switch (c) {
            case 'Z':
                return "boolean";
            case 'B':
                return "byte";
            case 'C':
                return "char";
            case 'S':
                return "short";
            case 'I':
                return "int";
            case 'J':
                return "long";
            case 'F':
                return "float";
            case 'D':
                return "double";
            default:
                throw new SignatureSyntaxError(c + " is not a primitive type!", sc);
        }
    }

    /**
     * Parse a JNI type; can be either a primitive or object type. Arrays are handled separately.
     *
     * @param sc Cursor over the string assumed to contain a JNI type.
     * @return String containing parsed JNI type.
     */
    private static String parseJniTypeWithoutArrayDimensions(StringCursor sc)
            throws SignatureSyntaxError, StringCursorOutOfBoundsException {
        char c = sc.peek();
        if (PRIMITIVE_TYPES.indexOf(c) != -1) {
            return parseJNIPrimitiveType(sc);
        } else if (c == 'L') {
            return parseJNIClassDescriptor(sc);
        }
        throw new SignatureSyntaxError("Illegal token " + c + " within signature", sc);
    }

    /**
     * Parse a JNI type.
     *
     * This parameter can be an array, in which case it will be preceded by a number of open square
     * brackets (corresponding to its dimensionality)
     *
     * @param sc Cursor over the string assumed to contain a JNI type.
     * @return Same as {@link #parseJniTypeWithoutArrayDimensions}, but also handle arrays.
     */
    private static String parseJniType(StringCursor sc)
            throws SignatureSyntaxError, StringCursorOutOfBoundsException {
        int arrayDimension = 0;
        while (sc.peek() == '[') {
            ++arrayDimension;
            sc.next();
        }
        StringBuilder sb = new StringBuilder();
        sb.append(parseJniTypeWithoutArrayDimensions(sc));
        for (int i = 0; i < arrayDimension; ++i) {
            sb.append("[]");
        }
        return sb.toString();
    }

    /**
     * Converts the parameters of method from JNI notation to Javadoc link notation. e.g.
     * "(IILfoo/bar/Baz;)V" turns into "int, int, foo.bar.Baz". The parentheses and return type are
     * discarded.
     *
     * @param sc Cursor over the string assumed to contain a JNI method parameters.
     * @return Comma separated list of parameter types.
     */
    private static String convertJNIMethodParametersToJavadoc(StringCursor sc)
            throws SignatureSyntaxError, StringCursorOutOfBoundsException {
        List<String> methodParameterTypes = new ArrayList<>();
        if (sc.next() != '(') {
            throw new IllegalArgumentException("Trying to parse method params of an invalid dex " +
                    "signature: " + sc.getOriginalString());
        }
        while (sc.peek() != ')') {
            methodParameterTypes.add(parseJniType(sc));
        }
        return String.join(", ", methodParameterTypes);
    }

    /**
     * Generate ApiComponents from a dex signature.
     *
     * This is used to extract the necessary context for an alternative API to try to infer missing
     * information.
     *
     * @param signature Dex signature.
     * @return ApiComponents instance with populated package, class name, and parameter types if
     * applicable.
     */
    public static ApiComponents fromDexSignature(String signature) throws SignatureSyntaxError {
        StringCursor sc = new StringCursor(signature);
        try {
            String fullyQualifiedClass = parseJNIClassDescriptor(sc);

            PackageAndClassName packageAndClassName =
                    PackageAndClassName.splitClassName(fullyQualifiedClass);
            if (!sc.peek(2).equals("->")) {
                throw new SignatureSyntaxError("Expected '->'", sc);
            }
            // Consume "->"
            sc.next(2);
            String memberName = "";
            String methodParameterTypes = "";
            int leftParenPos = sc.find('(');
            if (leftParenPos != -1) {
                memberName = sc.next(leftParenPos);
                methodParameterTypes = convertJNIMethodParametersToJavadoc(sc);
            } else {
                int colonPos = sc.find(':');
                if (colonPos == -1) {
                    throw new IllegalArgumentException("Expected : or -> beyond position "
                            + sc.position() + " in " + signature);
                } else {
                    memberName = sc.next(colonPos);
                    // Consume the ':'.
                    sc.next();
                    // Consume the type.
                    parseJniType(sc);
                }
            }
            return new ApiComponents(packageAndClassName, memberName, methodParameterTypes);
        } catch (StringCursorOutOfBoundsException e) {
            throw new SignatureSyntaxError(
                    "Unexpectedly reached end of string while trying to parse signature ", sc);
        }
    }

    /**
     * Generate ApiComponents from a link tag.
     *
     * @param linkTag          The contents of a link tag.
     * @param contextSignature The signature of the private API that this is an alternative for.
     *                         Used to infer unspecified components.
     */
    public static ApiComponents fromLinkTag(String linkTag, String contextSignature)
            throws JavadocLinkSyntaxError {
        ApiComponents contextAlternative;
        try {
            contextAlternative = fromDexSignature(contextSignature);
        } catch (SignatureSyntaxError e) {
            throw new RuntimeException(
                    "Failed to parse the context signature for public alternative!");
        }
        StringCursor sc = new StringCursor(linkTag);
        try {

            String memberName = "";
            String methodParameterTypes = "";

            int tagPos = sc.find('#');
            String fullyQualifiedClassName = sc.next(tagPos);

            PackageAndClassName packageAndClassName =
                    PackageAndClassName.splitClassName(fullyQualifiedClassName);

            if (packageAndClassName.packageName.isEmpty()) {
                packageAndClassName.packageName = contextAlternative.getPackageAndClassName()
                        .packageName;
            }

            if (packageAndClassName.className.isEmpty()) {
                packageAndClassName.className = contextAlternative.getPackageAndClassName()
                        .className;
            }

            if (tagPos == -1) {
                // This suggested alternative is just a class. We can allow that.
                return new ApiComponents(packageAndClassName, "", "");
            } else {
                // Consume the #.
                sc.next();
            }

            int leftParenPos = sc.find('(');
            memberName = sc.next(leftParenPos);
            if (leftParenPos != -1) {
                // Consume the '('.
                sc.next();
                int rightParenPos = sc.find(')');
                if (rightParenPos == -1) {
                    throw new JavadocLinkSyntaxError(
                            "Linked method is missing a closing parenthesis", sc);
                } else {
                    methodParameterTypes = sc.next(rightParenPos);
                }
            }

            return new ApiComponents(packageAndClassName, memberName, methodParameterTypes);
        } catch (StringCursorOutOfBoundsException e) {
            throw new JavadocLinkSyntaxError(
                    "Unexpectedly reached end of string while trying to parse javadoc link", sc);
        }
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof ApiComponents)) {
            return false;
        }
        ApiComponents other = (ApiComponents) obj;
        return mPackageAndClassName.equals(other.mPackageAndClassName) && mMemberName.equals(
                other.mMemberName) && mMethodParameterTypes.equals(other.mMethodParameterTypes);
    }

    @Override
    public int hashCode() {
        return Objects.hash(mPackageAndClassName, mMemberName, mMethodParameterTypes);
    }

    /**
     * Less restrictive comparator to use in case a link tag is missing a method's parameters.
     * e.g. foo.bar.Baz#foo will be considered the same as foo.bar.Baz#foo(int, int) and
     * foo.bar.Baz#foo(long, long). If the class only has one method with that name, then specifying
     * its parameter types is optional within the link tag.
     */
    public boolean equalsIgnoringParam(ApiComponents other) {
        return mPackageAndClassName.equals(other.mPackageAndClassName) &&
                mMemberName.equals(other.mMemberName);
    }
}
