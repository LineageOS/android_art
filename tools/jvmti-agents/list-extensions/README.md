# listextensions

listextensions is a jvmti agent that will print the details of all available jvmti extension
functions and events.

# Usage
### Build
>    `m liblistextensions`

The libraries will be built for 32-bit, 64-bit, host and target. Below examples
assume you want to use the 64-bit version.

#### ART
>    `art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so '-agentpath:liblistextensions.so' -cp tmp/java/helloworld.dex -Xint helloworld`

This will print something similar to:
```
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:104] Found 13 extension functions
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:107] com.android.art.heap.get_object_heap_id
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:108]       desc: Retrieve the heap id of the object tagged with the given argument. An arbitrary object is chosen if multiple objects exist with the same tag.
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:109]       arguments: (count: 2)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:112]               tag (IN, JLONG)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:112]               heap_id (OUT, JINT)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:114]       Errors: (count: 1)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:118]               JVMTI_ERROR_NOT_FOUND
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:107] com.android.art.heap.get_heap_name
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:108]       desc: Retrieve the name of the heap with the given id.
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:109]       arguments: (count: 2)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:112]               heap_id (IN, JINT)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:112]               heap_name (ALLOC_BUF, CCHAR)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:114]       Errors: (count: 1)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:118]               JVMTI_ERROR_ILLEGAL_ARGUMENT
...
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:130] Found 2 extension events
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:133] com.android.art.internal.ddm.publish_chunk
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:134]       index: 86
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:135]       desc: Called when there is new ddms information that the agent or other clients can use. The agent is given the 'type' of the ddms chunk and a 'data_size' byte-buffer in 'data'. The 'data' pointer is only valid for the duration of the publish_chunk event. The agent is responsible for interpreting the information present in the 'data' buffer. This is provided for backwards-compatibility support only. Agents should prefer to use relevant JVMTI events and functions above listening for this event.
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:136]       event arguments: (count: 4)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:139]               jni_env (IN_PTR, JNIENV)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:139]               type (IN, JINT)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:139]               data_size (IN, JINT)
dalvikvm64 I 07-30 10:47:37 154719 154719 list-extensions.cc:139]               data (IN_BUF, JBYTE)
...
```

* `-Xplugin` and `-agentpath` need to be used, otherwise the agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

>    `adb shell setenforce 0`
>
>    `adb push $ANDROID_PRODUCT_OUT/system/lib64/liblistextensions.so /data/local/tmp/`
>
>    `adb shell am start-activity --attach-agent /data/local/tmp/liblistextensions.so some.debuggable.apps/.the.app.MainActivity`

#### RI
>    `java -agentpath:liblistextensions.so -cp tmp/helloworld/classes helloworld`
