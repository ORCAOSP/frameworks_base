/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaCodec-JNI"
#include <utils/Log.h>

#include "android_media_MediaCodec.h"

#include "android_media_Utils.h"
#include "android_runtime/AndroidRuntime.h"
#include "android_runtime/android_view_Surface.h"
#include "jni.h"
#include "JNIHelp.h"

#include <gui/Surface.h>
#include <gui/SurfaceTextureClient.h>

#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>

namespace android {

// Keep these in sync with their equivalents in MediaCodec.java !!!
enum {
    DEQUEUE_INFO_TRY_AGAIN_LATER            = -1,
    DEQUEUE_INFO_OUTPUT_FORMAT_CHANGED      = -2,
    DEQUEUE_INFO_OUTPUT_BUFFERS_CHANGED     = -3,
};

struct fields_t {
    jfieldID context;
};

static fields_t gFields;

////////////////////////////////////////////////////////////////////////////////

JMediaCodec::JMediaCodec(
        JNIEnv *env, jobject thiz,
        const char *name, bool nameIsType, bool encoder)
    : mClass(NULL),
      mObject(NULL) {
    jclass clazz = env->GetObjectClass(thiz);
    CHECK(clazz != NULL);

    mClass = (jclass)env->NewGlobalRef(clazz);
    mObject = env->NewWeakGlobalRef(thiz);

    mLooper = new ALooper;
    mLooper->setName("MediaCodec_looper");

    mLooper->start(
            false,      // runOnCallingThread
            false,       // canCallJava
            PRIORITY_DEFAULT);

    if (nameIsType) {
        mCodec = MediaCodec::CreateByType(mLooper, name, encoder);
    } else {
        mCodec = MediaCodec::CreateByComponentName(mLooper, name);
    }
}

status_t JMediaCodec::initCheck() const {
    return mCodec != NULL ? OK : NO_INIT;
}

JMediaCodec::~JMediaCodec() {
    mCodec->stop();

    JNIEnv *env = AndroidRuntime::getJNIEnv();

    env->DeleteWeakGlobalRef(mObject);
    mObject = NULL;
    env->DeleteGlobalRef(mClass);
    mClass = NULL;
}

status_t JMediaCodec::configure(
        const sp<AMessage> &format,
        const sp<ISurfaceTexture> &surfaceTexture,
        int flags) {
    sp<SurfaceTextureClient> client;
    if (surfaceTexture != NULL) {
        client = new SurfaceTextureClient(surfaceTexture);
    }
    return mCodec->configure(format, client, flags);
}

status_t JMediaCodec::start() {
    return mCodec->start();
}

status_t JMediaCodec::stop() {
    return mCodec->stop();
}

status_t JMediaCodec::flush() {
    return mCodec->flush();
}

status_t JMediaCodec::queueInputBuffer(
        size_t index,
        size_t offset, size_t size, int64_t timeUs, uint32_t flags) {
    return mCodec->queueInputBuffer(index, offset, size, timeUs, flags);
}

status_t JMediaCodec::dequeueInputBuffer(size_t *index, int64_t timeoutUs) {
    return mCodec->dequeueInputBuffer(index, timeoutUs);
}

status_t JMediaCodec::dequeueOutputBuffer(
        JNIEnv *env, jobject bufferInfo, size_t *index, int64_t timeoutUs) {
    size_t size, offset;
    int64_t timeUs;
    uint32_t flags;
    status_t err;
    if ((err = mCodec->dequeueOutputBuffer(
                    index, &size, &offset, &timeUs, &flags, timeoutUs)) != OK) {
        return err;
    }

    jclass clazz = env->FindClass("android/media/MediaCodec$BufferInfo");

    jmethodID method = env->GetMethodID(clazz, "set", "(IIJI)V");
    env->CallVoidMethod(bufferInfo, method, offset, size, timeUs, flags);

    return OK;
}

status_t JMediaCodec::releaseOutputBuffer(size_t index, bool render) {
    return render
        ? mCodec->renderOutputBufferAndRelease(index)
        : mCodec->releaseOutputBuffer(index);
}

status_t JMediaCodec::getOutputFormat(JNIEnv *env, jobject *format) const {
    sp<AMessage> msg;
    status_t err;
    if ((err = mCodec->getOutputFormat(&msg)) != OK) {
        return err;
    }

    return ConvertMessageToMap(env, msg, format);
}

status_t JMediaCodec::getBuffers(
        JNIEnv *env, bool input, jobjectArray *bufArray) const {
    Vector<sp<ABuffer> > buffers;

    status_t err =
        input
            ? mCodec->getInputBuffers(&buffers)
            : mCodec->getOutputBuffers(&buffers);

    if (err != OK) {
        return err;
    }

    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");

    *bufArray = (jobjectArray)env->NewObjectArray(
            buffers.size(), byteBufferClass, NULL);

    for (size_t i = 0; i < buffers.size(); ++i) {
        const sp<ABuffer> &buffer = buffers.itemAt(i);

        jobject byteBuffer =
            env->NewDirectByteBuffer(
                buffer->base(),
                buffer->capacity());

        env->SetObjectArrayElement(
                *bufArray, i, byteBuffer);

        env->DeleteLocalRef(byteBuffer);
        byteBuffer = NULL;
    }

    return OK;
}

}  // namespace android

////////////////////////////////////////////////////////////////////////////////

using namespace android;

static sp<JMediaCodec> setMediaCodec(
        JNIEnv *env, jobject thiz, const sp<JMediaCodec> &codec) {
    sp<JMediaCodec> old = (JMediaCodec *)env->GetIntField(thiz, gFields.context);
    if (codec != NULL) {
        codec->incStrong(thiz);
    }
    if (old != NULL) {
        old->decStrong(thiz);
    }
    env->SetIntField(thiz, gFields.context, (int)codec.get());

    return old;
}

static sp<JMediaCodec> getMediaCodec(JNIEnv *env, jobject thiz) {
    return (JMediaCodec *)env->GetIntField(thiz, gFields.context);
}

static void android_media_MediaCodec_release(JNIEnv *env, jobject thiz) {
    setMediaCodec(env, thiz, NULL);
}

static jint throwExceptionAsNecessary(JNIEnv *env, status_t err) {
    switch (err) {
        case OK:
            return 0;

        case -EAGAIN:
            return DEQUEUE_INFO_TRY_AGAIN_LATER;

        case INFO_FORMAT_CHANGED:
            return DEQUEUE_INFO_OUTPUT_FORMAT_CHANGED;

        case INFO_OUTPUT_BUFFERS_CHANGED:
            return DEQUEUE_INFO_OUTPUT_BUFFERS_CHANGED;

        default:
        {
            jniThrowException(env, "java/lang/IllegalStateException", NULL);
            break;
        }
    }

    return 0;
}

static void android_media_MediaCodec_native_configure(
        JNIEnv *env,
        jobject thiz,
        jobjectArray keys, jobjectArray values,
        jobject jsurface,
        jint flags) {
    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    sp<AMessage> format;
    status_t err = ConvertKeyValueArraysToMessage(env, keys, values, &format);

    if (err != OK) {
        jniThrowException(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }

    sp<ISurfaceTexture> surfaceTexture;
    if (jsurface != NULL) {
        sp<Surface> surface(Surface_getSurface(env, jsurface));
        if (surface != NULL) {
            surfaceTexture = surface->getSurfaceTexture();
        } else {
            jniThrowException(
                    env,
                    "java/lang/IllegalArgumentException",
                    "The surface has been released");
            return;
        }
    }

    err = codec->configure(format, surfaceTexture, flags);

    throwExceptionAsNecessary(env, err);
}

static void android_media_MediaCodec_start(JNIEnv *env, jobject thiz) {
    ALOGV("android_media_MediaCodec_start");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    status_t err = codec->start();

    throwExceptionAsNecessary(env, err);
}

static void android_media_MediaCodec_stop(JNIEnv *env, jobject thiz) {
    ALOGV("android_media_MediaCodec_stop");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    status_t err = codec->stop();

    throwExceptionAsNecessary(env, err);
}

static void android_media_MediaCodec_flush(JNIEnv *env, jobject thiz) {
    ALOGV("android_media_MediaCodec_flush");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    status_t err = codec->flush();

    throwExceptionAsNecessary(env, err);
}

static void android_media_MediaCodec_queueInputBuffer(
        JNIEnv *env,
        jobject thiz,
        jint index,
        jint offset,
        jint size,
        jlong timestampUs,
        jint flags) {
    ALOGV("android_media_MediaCodec_queueInputBuffer");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    status_t err = codec->queueInputBuffer(
            index, offset, size, timestampUs, flags);

    throwExceptionAsNecessary(env, err);
}

static jint android_media_MediaCodec_dequeueInputBuffer(
        JNIEnv *env, jobject thiz, jlong timeoutUs) {
    ALOGV("android_media_MediaCodec_dequeueInputBuffer");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return -1;
    }

    size_t index;
    status_t err = codec->dequeueInputBuffer(&index, timeoutUs);

    if (err == OK) {
        return index;
    }

    return throwExceptionAsNecessary(env, err);
}

static jint android_media_MediaCodec_dequeueOutputBuffer(
        JNIEnv *env, jobject thiz, jobject bufferInfo, jlong timeoutUs) {
    ALOGV("android_media_MediaCodec_dequeueOutputBuffer");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return NULL;
    }

    size_t index;
    status_t err = codec->dequeueOutputBuffer(
            env, bufferInfo, &index, timeoutUs);

    if (err == OK) {
        return index;
    }

    return throwExceptionAsNecessary(env, err);
}

static void android_media_MediaCodec_releaseOutputBuffer(
        JNIEnv *env, jobject thiz, jint index, jboolean render) {
    ALOGV("android_media_MediaCodec_renderOutputBufferAndRelease");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return;
    }

    status_t err = codec->releaseOutputBuffer(index, render);

    throwExceptionAsNecessary(env, err);
}

static jobject android_media_MediaCodec_getOutputFormat(
        JNIEnv *env, jobject thiz) {
    ALOGV("android_media_MediaCodec_getOutputFormat");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return NULL;
    }

    jobject format;
    status_t err = codec->getOutputFormat(env, &format);

    if (err == OK) {
        return format;
    }

    throwExceptionAsNecessary(env, err);

    return NULL;
}

static jobjectArray android_media_MediaCodec_getBuffers(
        JNIEnv *env, jobject thiz, jboolean input) {
    ALOGV("android_media_MediaCodec_getBuffers");

    sp<JMediaCodec> codec = getMediaCodec(env, thiz);

    if (codec == NULL) {
        jniThrowException(env, "java/lang/IllegalStateException", NULL);
        return NULL;
    }

    jobjectArray buffers;
    status_t err = codec->getBuffers(env, input, &buffers);

    if (err == OK) {
        return buffers;
    }

    throwExceptionAsNecessary(env, err);

    return NULL;
}

static void android_media_MediaCodec_native_init(JNIEnv *env) {
    jclass clazz = env->FindClass("android/media/MediaCodec");
    CHECK(clazz != NULL);

    gFields.context = env->GetFieldID(clazz, "mNativeContext", "I");
    CHECK(gFields.context != NULL);
}

static void android_media_MediaCodec_native_setup(
        JNIEnv *env, jobject thiz,
        jstring name, jboolean nameIsType, jboolean encoder) {
    if (name == NULL) {
        jniThrowException(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }

    const char *tmp = env->GetStringUTFChars(name, NULL);

    if (tmp == NULL) {
        return;
    }

    sp<JMediaCodec> codec = new JMediaCodec(env, thiz, tmp, nameIsType, encoder);

    status_t err = codec->initCheck();

    env->ReleaseStringUTFChars(name, tmp);
    tmp = NULL;

    if (err != OK) {
        jniThrowException(
                env,
                "java/io/IOException",
                "Failed to allocate component instance");
        return;
    }

    setMediaCodec(env,thiz, codec);
}

static void android_media_MediaCodec_native_finalize(
        JNIEnv *env, jobject thiz) {
    android_media_MediaCodec_release(env, thiz);
}

static JNINativeMethod gMethods[] = {
    { "release", "()V", (void *)android_media_MediaCodec_release },

    { "native_configure",
      "([Ljava/lang/String;[Ljava/lang/Object;Landroid/view/Surface;I)V",
      (void *)android_media_MediaCodec_native_configure },

    { "start", "()V", (void *)android_media_MediaCodec_start },
    { "stop", "()V", (void *)android_media_MediaCodec_stop },
    { "flush", "()V", (void *)android_media_MediaCodec_flush },

    { "queueInputBuffer", "(IIIJI)V",
      (void *)android_media_MediaCodec_queueInputBuffer },

    { "dequeueInputBuffer", "(J)I",
      (void *)android_media_MediaCodec_dequeueInputBuffer },

    { "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I",
      (void *)android_media_MediaCodec_dequeueOutputBuffer },

    { "releaseOutputBuffer", "(IZ)V",
      (void *)android_media_MediaCodec_releaseOutputBuffer },

    { "getOutputFormat", "()Ljava/util/Map;",
      (void *)android_media_MediaCodec_getOutputFormat },

    { "getBuffers", "(Z)[Ljava/nio/ByteBuffer;",
      (void *)android_media_MediaCodec_getBuffers },

    { "native_init", "()V", (void *)android_media_MediaCodec_native_init },

    { "native_setup", "(Ljava/lang/String;ZZ)V",
      (void *)android_media_MediaCodec_native_setup },

    { "native_finalize", "()V",
      (void *)android_media_MediaCodec_native_finalize },
};

int register_android_media_MediaCodec(JNIEnv *env) {
    return AndroidRuntime::registerNativeMethods(env,
                "android/media/MediaCodec", gMethods, NELEM(gMethods));
}