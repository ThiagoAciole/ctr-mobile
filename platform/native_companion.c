#include <platform/native_companion.h>
#include <platform/native_log.h>
#include <platform/native_renderer.h>

#if defined(__ANDROID__)

#include <SDL3/SDL.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>

extern SDL_Window *g_window;

// Written by the Android UI thread in the JNI callbacks, read by the game
// thread. Only the game thread ever touches EGL, so the handoff is a pointer
// plus a size and a "this is new" flag.
global_variable ANativeWindow *s_companionWindow = NULL;
global_variable volatile int s_companionWindowDirty = 0;
global_variable volatile int s_companionPendingW = 0;
global_variable volatile int s_companionPendingH = 0;

global_variable EGLSurface s_companionSurface = EGL_NO_SURFACE;
global_variable int s_companionSurfaceWidth = 0;
global_variable int s_companionSurfaceHeight = 0;
global_variable int s_companionRequested = 0;
global_variable int s_companionFailed = 0;

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeCompanionSurfaceChanged(JNIEnv *env, jclass cls, jobject surface, jint width, jint height)
{
	(void)cls;

	ANativeWindow *previous = s_companionWindow;

	s_companionWindow = (surface != NULL) ? ANativeWindow_fromSurface(env, surface) : NULL;
	s_companionPendingW = (int)width;
	s_companionPendingH = (int)height;
	s_companionWindowDirty = 1;

	if (previous != NULL)
	{
		ANativeWindow_release(previous);
	}

	Platform_Log("[CTR-DS] companion surface %dx%d\n", (int)width, (int)height);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeCompanionSurfaceDestroyed(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	ANativeWindow *previous = s_companionWindow;

	s_companionWindow = NULL;
	s_companionWindowDirty = 1;

	if (previous != NULL)
	{
		ANativeWindow_release(previous);
	}

	Platform_Log("[CTR-DS] companion surface destroyed\n");
}

internal void NativeCompanion_CallJavaStatic(const char *method)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (env == NULL)
	{
		Platform_Log("[CTR-DS] no JNIEnv; cannot call %s\n", method);
		return;
	}

	jclass cls = (*env)->FindClass(env, "com/ctrnative/CTRNativeActivity");
	if (cls == NULL)
	{
		(*env)->ExceptionClear(env);
		Platform_Log("[CTR-DS] CTRNativeActivity not found\n");
		return;
	}

	jmethodID mid = (*env)->GetStaticMethodID(env, cls, method, "()V");
	if (mid != NULL)
	{
		(*env)->CallStaticVoidMethod(env, cls, mid);
	}
	else
	{
		(*env)->ExceptionClear(env);
		Platform_Log("[CTR-DS] method %s not found\n", method);
	}

	(*env)->DeleteLocalRef(env, cls);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativePanelTap(JNIEnv *env, jclass cls, jfloat nx, jfloat ny)
{
	(void)env;
	(void)cls;

	Ctrds_PanelTap((float)nx, (float)ny);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeCompanionUnavailable(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	// Java looked for a presentation display and found none, so this device has
	// only one screen. Fall back rather than stacking both rectangles into it.
	Ctrds_DisableSecondScreen();
}

void NativeCompanion_Init(void)
{
	if (s_companionRequested)
	{
		return;
	}

	s_companionRequested = 1;
	NativeCompanion_CallJavaStatic("startCompanionDisplay");
}

// Creates or tears down the EGL surface to match whatever Java last handed us.
internal void NativeCompanion_SyncSurface(void)
{
	if (!s_companionWindowDirty)
	{
		return;
	}

	s_companionWindowDirty = 0;

	EGLDisplay display = (EGLDisplay)SDL_EGL_GetCurrentDisplay();
	EGLConfig config = (EGLConfig)SDL_EGL_GetCurrentConfig();

	if ((display == EGL_NO_DISPLAY) || (config == NULL))
	{
		Platform_Log("[CTR-DS] no EGL display/config yet\n");
		return;
	}

	if (s_companionSurface != EGL_NO_SURFACE)
	{
		eglDestroySurface(display, s_companionSurface);
		s_companionSurface = EGL_NO_SURFACE;
	}

	if (s_companionWindow == NULL)
	{
		s_companionSurfaceWidth = 0;
		s_companionSurfaceHeight = 0;
		return;
	}

	// Android requires the window's buffer format to match the EGLConfig's
	// native visual, or eglCreateWindowSurface fails with EGL_BAD_MATCH.
	{
		EGLint nativeVisualId = 0;
		if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeVisualId))
		{
			ANativeWindow_setBuffersGeometry(s_companionWindow, 0, 0, nativeVisualId);
		}
	}

	s_companionSurface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)s_companionWindow, NULL);
	if (s_companionSurface == EGL_NO_SURFACE)
	{
		s_companionFailed = 1;
		Platform_Log("[CTR-DS] eglCreateWindowSurface failed: 0x%x\n", eglGetError());
		return;
	}

	s_companionSurfaceWidth = s_companionPendingW;
	s_companionSurfaceHeight = s_companionPendingH;
	s_companionFailed = 0;

	Platform_Log("[CTR-DS] companion EGLSurface ready %dx%d\n", s_companionSurfaceWidth, s_companionSurfaceHeight);
}

int NativeCompanion_IsReady(void)
{
	NativeCompanion_SyncSurface();
	return (s_companionSurface != EGL_NO_SURFACE) && (s_companionSurfaceWidth > 0) && (s_companionSurfaceHeight > 0);
}

void NativeCompanion_Present(int vramX, int vramY, int vramW, int vramH)
{
	if (s_companionSurface == EGL_NO_SURFACE)
	{
		return;
	}

	EGLDisplay display = eglGetCurrentDisplay();
	EGLContext context = eglGetCurrentContext();

	// The surface that is current right now IS the main window's surface, which
	// is more dependable than asking SDL for it.
	EGLSurface mainSurface = eglGetCurrentSurface(EGL_DRAW);

	{
		global_variable int loggedOnce = 0;
		if (!loggedOnce)
		{
			loggedOnce = 1;
			Platform_Log("[CTR-DS] present: display=%p context=%p main=%p companion=%p surface=%dx%d\n", (void *)display, (void *)context,
			        (void *)mainSurface, (void *)s_companionSurface, s_companionSurfaceWidth, s_companionSurfaceHeight);
		}
	}

	if ((display == EGL_NO_DISPLAY) || (context == EGL_NO_CONTEXT))
	{
		return;
	}

	if (!eglMakeCurrent(display, s_companionSurface, s_companionSurface, context))
	{
		Platform_Log("[CTR-DS] eglMakeCurrent(companion) failed: 0x%x\n", eglGetError());
		return;
	}

	// The game's frame pacing comes from the main surface. Leaving this one
	// vsync-locked too serialises the two swaps and halves the frame rate.
	eglSwapInterval(display, 0);

	NativeRenderer_PresentVRAMRectToViewport(vramX, vramY, vramW, vramH, s_companionSurfaceWidth, s_companionSurfaceHeight);

	if (!eglSwapBuffers(display, s_companionSurface))
	{
		global_variable int loggedSwapFail = 0;
		if (!loggedSwapFail)
		{
			loggedSwapFail = 1;
			Platform_Log("[CTR-DS] eglSwapBuffers(companion) failed: 0x%x\n", eglGetError());
		}
	}

	if ((mainSurface != EGL_NO_SURFACE) && !eglMakeCurrent(display, mainSurface, mainSurface, context))
	{
		Platform_Log("[CTR-DS] eglMakeCurrent(main) failed: 0x%x\n", eglGetError());
	}
}

void NativeCompanion_Shutdown(void)
{
	EGLDisplay display = (EGLDisplay)SDL_EGL_GetCurrentDisplay();

	if ((s_companionSurface != EGL_NO_SURFACE) && (display != EGL_NO_DISPLAY))
	{
		eglDestroySurface(display, s_companionSurface);
	}
	s_companionSurface = EGL_NO_SURFACE;

	if (s_companionWindow != NULL)
	{
		ANativeWindow_release(s_companionWindow);
		s_companionWindow = NULL;
	}

	if (s_companionRequested)
	{
		NativeCompanion_CallJavaStatic("stopCompanionDisplay");
		s_companionRequested = 0;
	}
}

#else // !__ANDROID__

void NativeCompanion_Init(void)
{
}

int NativeCompanion_IsReady(void)
{
	return 0;
}

void NativeCompanion_Present(int vramX, int vramY, int vramW, int vramH)
{
	(void)vramX;
	(void)vramY;
	(void)vramW;
	(void)vramH;
}

void NativeCompanion_Shutdown(void)
{
}

#endif
