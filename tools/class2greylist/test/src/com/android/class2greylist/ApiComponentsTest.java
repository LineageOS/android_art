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

import static com.google.common.truth.Truth.assertThat;

import static org.testng.Assert.assertThrows;

import org.junit.Test;


public class ApiComponentsTest extends AnnotationHandlerTestBase {

    @Test
    public void testGetApiComponentsPackageFromSignature() throws SignatureSyntaxError {
        ApiComponents api = ApiComponents.fromDexSignature("La/b/C;->foo()V");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("a.b");
    }

    @Test
    public void testGetApiComponentsFromSignature() throws SignatureSyntaxError {
        ApiComponents api = ApiComponents.fromDexSignature("La/b/C;->foo(IJLfoo2/bar/Baz;)V");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.className).isEqualTo("C");
        assertThat(api.getMemberName()).isEqualTo("foo");
        assertThat(api.getMethodParameterTypes()).isEqualTo("int, long, foo2.bar.Baz");
    }

    @Test
    public void testInvalidDexSignatureInvalidClassFormat() throws SignatureSyntaxError {
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("a/b/C;->foo()V");
        });
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("La/b/C->foo()V");
        });
    }

    @Test
    public void testInvalidDexSignatureInvalidParameterType() throws SignatureSyntaxError {
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("a/b/C;->foo(foo)V");
        });
    }

    @Test
    public void testInvalidDexSignatureInvalidReturnType() throws SignatureSyntaxError {
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("a/b/C;->foo()foo");
        });
    }

    @Test
    public void testInvalidDexSignatureMissingReturnType() throws SignatureSyntaxError {
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("a/b/C;->foo(I)");
        });
    }

    @Test
    public void testInvalidDexSignatureMissingArrowOrColon() throws SignatureSyntaxError {
        assertThrows(SignatureSyntaxError.class, () -> {
            ApiComponents.fromDexSignature("La/b/C;foo()V");
        });
    }

    @Test
    public void testGetApiComponentsFromFieldLink() throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("a.b.C#foo(int, long, foo2.bar.Baz)",
                "La/b/C;->foo:I");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("a.b");
        assertThat(packageAndClassName.className).isEqualTo("C");
        assertThat(api.getMemberName()).isEqualTo("foo");
    }

    @Test
    public void testGetApiComponentsLinkOnlyClass() throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("b.c.D", "La/b/C;->foo:I");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("b.c");
        assertThat(packageAndClassName.className).isEqualTo("D");
        assertThat(api.getMethodParameterTypes()).isEqualTo("");
    }

    @Test
    public void testGetApiComponentsFromLinkOnlyClassDeducePackage() throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("D", "La/b/C;->foo:I");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("a.b");
        assertThat(packageAndClassName.className).isEqualTo("D");
        assertThat(api.getMemberName().isEmpty()).isTrue();
        assertThat(api.getMethodParameterTypes().isEmpty()).isTrue();
    }

    @Test
    public void testGetApiComponentsParametersFromMethodLink() throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("a.b.C#foo(int, long, foo2.bar.Baz)",
                "La/b/C;->foo:I");
        assertThat(api.getMethodParameterTypes()).isEqualTo("int, long, foo2.bar.Baz");
    }

    @Test
    public void testDeduceApiComponentsPackageFromLinkUsingContext() throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("C#foo(int, long, foo2.bar.Baz)",
                "La/b/C;->foo:I");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("a.b");
    }

    @Test
    public void testDeduceApiComponentsPackageAndClassFromLinkUsingContext()
            throws JavadocLinkSyntaxError {
        ApiComponents api = ApiComponents.fromLinkTag("#foo(int, long, foo2.bar.Baz)",
                "La/b/C;->foo:I");
        PackageAndClassName packageAndClassName = api.getPackageAndClassName();
        assertThat(packageAndClassName.packageName).isEqualTo("a.b");
        assertThat(packageAndClassName.className).isEqualTo("C");
    }

    @Test
    public void testInvalidLinkTagUnclosedParenthesis() throws JavadocLinkSyntaxError {
        assertThrows(JavadocLinkSyntaxError.class, () -> {
            ApiComponents.fromLinkTag("a.b.C#foo(int,float", "La/b/C;->foo()V");
        });
    }

}