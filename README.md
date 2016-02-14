# ace-ios

## Accessible Communications for Everyone

This is source tree for the ACE App for Mac.

## Building

This github project is automatically built via Travis.

The `.travis.yml` file is what directs these Travis automated builds. This includes all bootstrapping and preparation of a build environment, including all of the steps below.

These are the steps you can follow to build ace-ios locally.

1. Ensure you have Xcode installed on your mac.

2. Install Homebrew from http://brew.sh

3. Prepare your build environment:

    ./Tools/prepare.sh
    
4. Pull the ace-ios repo and init the submodules:
    
```
    git clone git@github.com:VTCSecureLLC/ace-ios.git
    cd ace-ios
    git submodule update --init --recursive
```


5. (re)Build the SDK:
    
```
    rm -fr WORK liblinphone-sdk ; ./prepare.py -d devices -G Ninja -DENABLE_WEBRTC_AEC=ON -DENABLE_H263=YES -DENABLE_FFMPEG=YES -DENABLE_H263=YES -DENABLE_AMRWB=YES -DENABLE_AMRNB=YES -DENABLE_OPENH264=YES -DENABLE_G729=YES -DENABLE_MPEG4=YES -DENABLE_H263P=YES -DENABLE_PCAP=YES --enable-non-free-codecs --enable-gpl-third-parties --build-all-codecs
    make -j 8
```

You should now see a liblinphone-sdk directory with Linphone SDK library build assets.

6. Build a debug build of the app:
    
```
    xcrun xcodebuild -project linphone.xcodeproj -scheme linphone -sdk iphoneos -configuration Debug build CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" CODE_SIGN_ENTITLEMENTS=""
```

This will build a `.app`, but will not generate an archive `.ipa` for a device. You will not be able to run this.

7.  If all of that went well, you should be able to run it in Xcode on your device. Use Xcode "run" to run the app on your device, or archive the app to an `.ipa` to distribute.

