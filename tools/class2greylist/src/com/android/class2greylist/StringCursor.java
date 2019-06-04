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

/**
 * Utility class to simplify parsing of signatures.
 */
public class StringCursor {

    private final String mString;
    private int mCursor;

    public StringCursor(String str) {
        mString = str;
        mCursor = 0;
    }

    /**
     * Position of cursor in string.
     *
     * @return Current position of cursor in string.
     */
    public int position() {
        return mCursor;
    }

    /**
     * Peek current cursor position.
     *
     * @return The character at the current cursor position.
     */
    public char peek() {
        return mString.charAt(mCursor);
    }

    /**
     * Peek several characters at the current cursor position without moving the cursor.
     *
     * @param n The number of characters to peek.
     * @return A string with x characters from the cursor position. If n is -1, return the whole
     *        rest of the string.
     */
    public String peek(int n) throws StringCursorOutOfBoundsException {
        if (n == -1) {
            return mString.substring(mCursor);
        }
        if (n < 0 || (n + mCursor) >= mString.length()) {
            throw new StringCursorOutOfBoundsException();
        }
        return mString.substring(mCursor, mCursor + n);
    }

    /**
     * Consume the character at the current cursor position and move the cursor forwards.
     *
     * @return The character at the current cursor position.
     */
    public char next() throws StringCursorOutOfBoundsException {
        if (!hasNext()) {
            throw new StringCursorOutOfBoundsException();
        }
        return mString.charAt(mCursor++);
    }

    /**
     * Consume several characters at the current cursor position and move the cursor further along.
     *
     * @param n The number of characters to consume.
     * @return A string with x characters from the cursor position. If n is -1, return the whole
     *         rest of the string.
     */
    public String next(int n) throws StringCursorOutOfBoundsException {
        if (n == -1) {
            String restOfString = mString.substring(mCursor);
            mCursor = mString.length();
            return restOfString;
        }
        if (n < 0) {
            throw new StringCursorOutOfBoundsException();
        }
        mCursor += n;
        return mString.substring(mCursor - n, mCursor);
    }

    /**
     * Search for the first occurrence of a character beyond the current cursor position.
     *
     * @param c The character to search for.
     * @return The offset of the first occurrence of c in the string beyond the cursor position.
     * If the character does not exist, return -1.
     */
    public int find(char c) {
        int firstIndex = mString.indexOf(c, mCursor);
        if (firstIndex == -1) {
            return -1;
        }
        return firstIndex - mCursor;
    }

    /**
     * Check if cursor has reached end of string.
     *
     * @return Cursor has reached end of string.
     */
    public boolean hasNext() {
        return mCursor < mString.length();
    }

    @Override
    public String toString() {
        return mString.substring(mCursor);
    }

    public String getOriginalString() {
        return mString;
    }
}