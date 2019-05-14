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

import java.util.Objects;

class PackageAndClassName{
    public String packageName;
    public String className;

    private PackageAndClassName(String packageName, String className) {
        this.packageName = packageName;
        this.className = className;
    }

    /**
     * Given a potentially fully qualified class name, split it into package and class.
     *
     * @param fullyQualifiedClassName potentially fully qualified class name.
     * @return A pair of strings, containing the package name (or empty if not specified) and
     * the
     * class name (or empty if string is empty).
     */
    public static PackageAndClassName splitClassName(String fullyQualifiedClassName) {
        int lastDotIdx = fullyQualifiedClassName.lastIndexOf('.');
        if (lastDotIdx == -1) {
            return new PackageAndClassName("", fullyQualifiedClassName);
        }
        String packageName = fullyQualifiedClassName.substring(0, lastDotIdx);
        String className = fullyQualifiedClassName.substring(lastDotIdx + 1);
        return new PackageAndClassName(packageName, className);
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof PackageAndClassName)) {
            return false;
        }
        PackageAndClassName other = (PackageAndClassName) obj;
        return Objects.equals(packageName, other.packageName) && Objects.equals(className,
                other.className);
    }

    @Override
    public String toString() {
        return packageName + "." + className;
    }

    @Override
    public int hashCode() {
        return Objects.hash(packageName, className);
    }
}
