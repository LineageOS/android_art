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

import com.google.common.base.Strings;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public class ApiResolver {
    private final List<ApiComponents> mPotentialPublicAlternatives;
    private final Set<PackageAndClassName> mPublicApiClasses;

    private static final Pattern LINK_TAG_PATTERN = Pattern.compile("\\{@link ([^\\}]+)\\}");
    private static final Pattern CODE_TAG_PATTERN = Pattern.compile("\\{@code ([^\\}]+)\\}");
    private static final Integer MIN_SDK_REQUIRING_PUBLIC_ALTERNATIVES = 29;

    public ApiResolver() {
        mPotentialPublicAlternatives = null;
        mPublicApiClasses = null;
    }

    public ApiResolver(Set<String> publicApis) {
        mPotentialPublicAlternatives = publicApis.stream()
                .map(api -> {
                    try {
                        return ApiComponents.fromDexSignature(api);
                    } catch (SignatureSyntaxError e) {
                        throw new RuntimeException("Could not parse public API signature:", e);
                    }
                })
                .collect(Collectors.toList());
        mPublicApiClasses = mPotentialPublicAlternatives.stream()
                .map(api -> api.getPackageAndClassName())
                .collect(Collectors.toCollection(HashSet::new));
    }

    /**
     * Verify that all public alternatives are valid.
     *
     * @param publicAlternativesString String containing public alternative explanations.
     * @param signature                Signature of the member that has the annotation.
     */
    public void resolvePublicAlternatives(String publicAlternativesString, String signature,
                                          Integer maxSdkVersion)
            throws JavadocLinkSyntaxError, AlternativeNotFoundError,
                    RequiredAlternativeNotSpecifiedError {
        if (Strings.isNullOrEmpty(publicAlternativesString) && maxSdkVersion != null
                && maxSdkVersion >= MIN_SDK_REQUIRING_PUBLIC_ALTERNATIVES) {
            throw new RequiredAlternativeNotSpecifiedError();
        }
        if (publicAlternativesString != null && mPotentialPublicAlternatives != null) {
            // Grab all instances of type {@link foo}
            Matcher matcher = LINK_TAG_PATTERN.matcher(publicAlternativesString);
            boolean hasLinkAlternative = false;
            // Validate all link tags
            while (matcher.find()) {
                hasLinkAlternative = true;
                String alternativeString = matcher.group(1);
                ApiComponents alternative = ApiComponents.fromLinkTag(alternativeString,
                        signature);
                if (alternative.getMemberName().isEmpty()) {
                    // Provided class as alternative
                    if (!mPublicApiClasses.contains(alternative.getPackageAndClassName())) {
                        throw new ClassAlternativeNotFoundError(alternative);
                    }
                } else if (!mPotentialPublicAlternatives.contains(alternative)) {
                    // If the link is not a public alternative, it must because the link does not
                    // contain the method parameter types, e.g. {@link foo.bar.Baz#foo} instead of
                    // {@link foo.bar.Baz#foo(int)}. If the method name is unique within the class,
                    // we can handle it.
                    if (!Strings.isNullOrEmpty(alternative.getMethodParameterTypes())) {
                        throw new MemberAlternativeNotFoundError(alternative);
                    }
                    List<ApiComponents> almostMatches = mPotentialPublicAlternatives.stream()
                            .filter(api -> api.equalsIgnoringParam(alternative))
                            .collect(Collectors.toList());
                    if (almostMatches.size() == 0) {
                        throw new MemberAlternativeNotFoundError(alternative);
                    } else if (almostMatches.size() > 1) {
                        throw new MultipleAlternativesFoundError(alternative, almostMatches);
                    }
                }
            }
            // No {@link ...} alternatives exist; try looking for {@code ...}
            if (!hasLinkAlternative) {
                if (!CODE_TAG_PATTERN.matcher(publicAlternativesString).find()) {
                    throw new NoAlternativesSpecifiedError();
                }
            }
        }
    }
}
