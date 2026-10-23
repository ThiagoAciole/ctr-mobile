#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include "platform/native_log.h"

void Platform_Android_PickFile(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "pickFile", "()V");

    if (methodId) {
        (*env)->CallStaticVoidMethod(env, clazz, methodId);
    } else {
        Platform_LogError("[CTR Android] Failed to find pickFile method\n");
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
}

void Platform_Android_OnMainMenuReady(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "onNativeMainMenuReady", "()V");

    if (methodId) {
        (*env)->CallStaticVoidMethod(env, clazz, methodId);
    } else {
        Platform_LogError("[CTR Android] Failed to find onNativeMainMenuReady method\n");
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
}

void Platform_Android_OnTouchScreenChanged(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "onNativeTouchScreenChanged", "()V");

    if (methodId) {
        (*env)->CallStaticVoidMethod(env, clazz, methodId);
    } else {
        Platform_LogError("[CTR Android] Failed to find onNativeTouchScreenChanged method\n");
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
}

void Platform_Android_ShowCampaignStorageError(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass clazz = (*env)->FindClass(env, "com/ctrnative/CTRNativeActivity");
    if (!clazz) {
        (*env)->ExceptionClear(env);
        return;
    }
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "showCampaignStorageError", "()V");
    if (methodId) {
        (*env)->CallStaticVoidMethod(env, clazz, methodId);
    } else {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, clazz);
}

char* Platform_Android_GetStoredPath(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return NULL;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return NULL;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return NULL;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "getStoredAssetPath", "()Ljava/lang/String;");

    char* result = NULL;
    if (methodId) {
        jstring jPath = (jstring)(*env)->CallStaticObjectMethod(env, clazz, methodId);
        if (jPath) {
            const char* pathChars = (*env)->GetStringUTFChars(env, jPath, NULL);
            if (pathChars) {
                result = SDL_strdup(pathChars);
                (*env)->ReleaseStringUTFChars(env, jPath, pathChars);
            }
            (*env)->DeleteLocalRef(env, jPath);
        }
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
    return result;
}

int Platform_Android_IsPickerActive(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return 0;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return 0;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return 0;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "isPickerActive", "()Z");

    jboolean result = JNI_FALSE;
    if (methodId) {
        result = (*env)->CallStaticBooleanMethod(env, clazz, methodId);
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
    return result == JNI_TRUE;
}

#endif
