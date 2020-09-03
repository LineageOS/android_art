# simpleprofile

simpleprofile is a JVMTI agent that lets one get simple JSON profiles with JVMTI

# Usage
### Build
>    `m libsimpleprofile`  # or 'm libsimpleprofiled' with debugging checks enabled

For binaries with NDK shared libraries only.
>    `m libsimpleprofiled` # or `m libsimpleprofileds` with debugging checks enabled.

The libraries will be built for 32-bit, 64-bit, host and target. Below examples
assume you want to use the 64-bit version.

### Command Line

The agent is loaded using -agentpath like normal. It takes arguments in the
following format:
>     `file-output[,dump_on_shutdown][,dump_on_main_stop]`


#### ART
>    `art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so '-agentpath:libsimpleprofiled.so=/proc/self/fd/2,dump_on_main_stop' -cp tmp/java/helloworld.dex -Xint helloworld`

* `-Xplugin` and `-agentpath` need to be used, otherwise the agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

#### Device
```
% adb root
% adb shell setenforce 0
% adb push $OUT/system/lib64/libsimpleprofileds.so /data/local/tmp/libsimpleprofileds.so
% adb shell
blueline:/data/data/com.google.android.apps.maps # cp /data/local/tmp/libsimpleprofileds.so .
blueline:/data/data/com.google.android.apps.maps # ps -A | grep maps
u0_a178        9143    927 15691440 190132 SyS_epoll_wait     0 S com.google.android.apps.maps
blueline:/data/data/com.google.android.apps.maps # cmd activity attach-agent com.google.android.apps.maps $PWD/libsimpleprofileds.so=$PWD/maps.json
blueline:/data/data/com.google.android.apps.maps # # Do things on the app.
blueline:/data/data/com.google.android.apps.maps # kill -3 9143
blueline:/data/data/com.google.android.apps.maps # wc -l maps.json
17901 maps.json
blueline:/data/data/com.google.android.apps.maps # ^D
% adb pull /data/data/com.google.android.apps.maps/maps.json
```

#### RI
>    `java '-agentpath:libsimpleprofiled.so=/proc/self/fd/2,dump_on_main_stop' -cp tmp/helloworld/classes helloworld`

### Output
A normal run will look something like this:

    % ./test/run-test --64 --host --dev --with-agent $ANDROID_HOST_OUT/lib64/libsimpleprofiled.so=dump_on_main_stop,/proc/self/fd/1 001-HelloWorld
    <normal output removed>
    Hello, world!
    ...
    {"class_name":"Ljava/util/HashMap$KeySet;","method_name":"iterator","method_descriptor":"()Ljava/util/Iterator;","count":6},
    {"class_name":"Ljava/util/HashMap$KeyIterator;","method_name":"<init>","method_descriptor":"(Ljava/util/HashMap;)V","count":6},
    {"class_name":"Ljava/util/HashMap$HashIterator;","method_name":"<init>","method_descriptor":"(Ljava/util/HashMap;)V","count":6},
    {"class_name":"Ljava/lang/String;","method_name":"equals","method_descriptor":"(Ljava/lang/Object;)Z","count":128},
    {"class_name":"Ljava/util/Collections$UnmodifiableCollection$1;","method_name":"next","method_descriptor":"()Ljava/lang/Object;","count":38},
    {"class_name":"Ljava/util/HashMap$KeyIterator;","method_name":"next","method_descriptor":"()Ljava/lang/Object;","count":38},
    {"class_name":"Lsun/misc/Cleaner;","method_name":"add","method_descriptor":"(Lsun/misc/Cleaner;)Lsun/misc/Cleaner;","count":1},
    ...
