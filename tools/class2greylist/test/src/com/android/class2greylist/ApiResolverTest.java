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

import static org.testng.Assert.expectThrows;
import static org.testng.Assert.assertThrows;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

public class ApiResolverTest extends AnnotationHandlerTestBase {
    @Test
    public void testFindPublicAlternativeExactly()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->foo(I)V", "La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("{@link a.b.C#foo(int)}", "Lb/c/D;->bar()V", 1);
    }

    @Test
    public void testFindPublicAlternativeDeducedPackageName()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->foo(I)V", "La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("{@link C#foo(int)}", "La/b/D;->bar()V", 1);
    }

    @Test
    public void testFindPublicAlternativeDeducedPackageAndClassName()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->foo(I)V", "La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("{@link #foo(int)}", "La/b/C;->bar()V", 1);
    }

    @Test
    public void testFindPublicAlternativeDeducedParameterTypes()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->foo(I)V", "La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("{@link #foo}", "La/b/C;->bar()V", 1);
    }

    @Test
    public void testFindPublicAlternativeFailDueToMultipleParameterTypes()
            throws SignatureSyntaxError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->foo(I)V", "La/b/C;->bar(I)I", "La/b/C;->foo(II)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        MultipleAlternativesFoundError e = expectThrows(MultipleAlternativesFoundError.class,
                () -> resolver.resolvePublicAlternatives("{@link #foo}", "La/b/C;->bar()V", 1));
        assertThat(e.almostMatches).containsExactly(
                ApiComponents.fromDexSignature("La/b/C;->foo(I)V"),
                ApiComponents.fromDexSignature("La/b/C;->foo(II)V")
        );
    }

    @Test
    public void testFindPublicAlternativeFailNoAlternative() {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        assertThrows(MemberAlternativeNotFoundError.class, ()
                -> resolver.resolvePublicAlternatives("{@link #foo(int)}", "La/b/C;->bar()V", 1));
    }

    @Test
    public void testFindPublicAlternativeFailNoAlternativeNoParameterTypes() {

        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        assertThrows(MemberAlternativeNotFoundError.class,
                () -> resolver.resolvePublicAlternatives("{@link #foo}", "La/b/C;->bar()V", 1));
    }

    @Test
    public void testNoPublicClassAlternatives() {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>());
        ApiResolver resolver = new ApiResolver(publicApis);
        expectThrows(NoAlternativesSpecifiedError.class,
                () -> resolver.resolvePublicAlternatives("Foo", "La/b/C;->bar()V", 1));
    }

    @Test
    public void testPublicAlternativesJustPackageAndClassName()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("Foo {@link a.b.C}", "Lb/c/D;->bar()V", 1);
    }

    @Test
    public void testPublicAlternativesJustClassName()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>(
                Arrays.asList("La/b/C;->bar(I)V")));
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("Foo {@link C}", "La/b/D;->bar()V", 1);
    }

    @Test
    public void testNoPublicAlternativesButHasExplanation()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>());
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives("Foo {@code bar}", "La/b/C;->bar()V", 1);
    }

    @Test
    public void testNoPublicAlternativesSpecifiedWithMaxSdk() {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>());
        ApiResolver resolver = new ApiResolver(publicApis);
        assertThrows(RequiredAlternativeNotSpecifiedError.class,
                () -> resolver.resolvePublicAlternatives(null, "La/b/C;->bar()V", 29));
    }

    @Test
    public void testNoPublicAlternativesSpecifiedWithMaxLessThanQ()
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
            RequiredAlternativeNotSpecifiedError {
        Set<String> publicApis = Collections.unmodifiableSet(new HashSet<>());
        ApiResolver resolver = new ApiResolver(publicApis);
        resolver.resolvePublicAlternatives(null, "La/b/C;->bar()V", 28);
    }

}