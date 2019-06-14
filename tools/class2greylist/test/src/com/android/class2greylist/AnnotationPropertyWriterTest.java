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

import com.google.common.collect.ImmutableMap;

import org.junit.Before;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;


public class AnnotationPropertyWriterTest {

    private ByteArrayOutputStream mByteArrayOutputStream;
    private AnnotationPropertyWriter mAnnotationPropertyWriter;

    @Before
    public void setup() {
        mByteArrayOutputStream = new ByteArrayOutputStream();
        mAnnotationPropertyWriter = new AnnotationPropertyWriter(mByteArrayOutputStream);
    }

    @Test
    public void testExportPropertiesNoEscaping() {
        String signature = "foo";
        Map<String, String> annotationProperties = ImmutableMap.of(
                "prop", "val"
        );
        Set<String> parsedFlags = new HashSet<String>();
        mAnnotationPropertyWriter.consume(signature, annotationProperties, parsedFlags);
        mAnnotationPropertyWriter.close();

        String output = mByteArrayOutputStream.toString();
        String expected = "prop,signature\n"
                + "|val|,|foo|\n";
        assertThat(output).isEqualTo(expected);
    }

    @Test
    public void testExportPropertiesEscapeQuotes() {
        String signature = "foo";
        Map<String, String> annotationProperties = ImmutableMap.of(
                "prop", "val1 | val2 | val3"
        );
        Set<String> parsedFlags = new HashSet<String>();
        mAnnotationPropertyWriter.consume(signature, annotationProperties, parsedFlags);
        mAnnotationPropertyWriter.close();

        String output = mByteArrayOutputStream.toString();
        String expected = "prop,signature\n"
                + "|val1 || val2 || val3|,|foo|\n";
        assertThat(output).isEqualTo(expected);
    }
}
