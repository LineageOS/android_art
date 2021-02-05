/*
 * Copyright (C) 2021 The Android Open Source Project
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

package com.android.server.art;

import static org.junit.Assert.assertEquals;

import com.android.server.art.ArtService;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;

public class ArtServiceTests {
    private static ArtService mArtService;

    @Before
    public void setup() {
        mArtService = new ArtService();
    }

    @Test
    public void testNotifyPrimaryUse() {
        assertEquals(mArtService.getMsg(), "Hello world!");
    }
}