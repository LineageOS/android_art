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

public class NoAlternativesSpecifiedError extends AlternativeNotFoundError {

    @Override
    public String toString() {
        return "Hidden API has a public alternative annotation field, but no concrete "
                + "explanations. Please provide either a reference to an SDK method using javadoc "
                + "syntax, e.g. {@link foo.bar.Baz#bat}, or a small code snippet if the "
                + "alternative is part of a support library or third party library, e.g. "
                + "{@code foo.bar.Baz bat = new foo.bar.Baz(); bat.doSomething();}.\n"
                + "If this is too restrictive for your use case, please contact compat-team@.";
    }
}
