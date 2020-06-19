#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LBadField;
.super Ljava/lang/Object;

# These are bad fields since there is no class Widget in this test.
.field public static widget:LWidget;
.field public iwidget:LWidget;

.method public constructor <init>()V
    .registers 2
    invoke-direct {v1}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public static storeStaticObject()V
    .registers 1
    new-instance v0, Ljava/lang/Object;
    invoke-direct {v0}, Ljava/lang/Object;-><init>()V
    sput-object v0, LBadField;->widget:LWidget;
    return-void
.end method

.method public static storeStaticNull()V
    .registers 1
    const/4 v0, 0
    sput-object v0, LBadField;->widget:LWidget;
    return-void
.end method

.method public static storeInstanceObject()V
    .registers 2
    new-instance v1, LBadField;
    invoke-direct {v1}, LBadField;-><init>()V
    new-instance v0, Ljava/lang/Object;
    invoke-direct {v0}, Ljava/lang/Object;-><init>()V
    iput-object v0, v1, LBadField;->iwidget:LWidget;
    return-void
.end method

.method public static storeInstanceNull()V
    .registers 2
    new-instance v1, LBadField;
    invoke-direct {v1}, LBadField;-><init>()V
    const/4 v0, 0
    iput-object v0, v1, LBadField;->iwidget:LWidget;
    return-void
.end method
