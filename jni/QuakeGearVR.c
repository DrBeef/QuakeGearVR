/************************************************************************************

Filename	:	QuakeGearVR.c based on VrCubeWorld_SurfaceView.c
Content		:	This sample uses a plain Android SurfaceView and handles all
				Activity and Surface life cycle events in native code. This sample
				does not use the application framework and also does not use LibOVR.
				This sample only uses the VrApi.
Created		:	March, 2015
Authors		:	J.M.P. van Waveren / Simon Brown

Copyright	:	Copyright 2015 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <android/native_window_jni.h>	// for native window JNI
#include <android/input.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <qtypes.h>
#include <menu.h>

#if !defined( EGL_OPENGL_ES3_BIT_KHR )
#define EGL_OPENGL_ES3_BIT_KHR		0x0040
#endif

#define PITCH 0
#define YAW 1
#define ROLL 2

#include "VrApi.h"
#include "VrApi_Helpers.h"
#include "VrApi_Android.h"

#define DEBUG 1
#define LOG_TAG "QuakeGearVR"

#define ALOGE(...) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__ )
#if DEBUG
#define ALOGV(...) __android_log_print( ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__ )
#else
#define ALOGV(...)
#endif

static const int CPU_LEVEL			= 2;
static const int GPU_LEVEL			= 3;

//All the functionality we link to in the DarkPlaces Quake implementation
extern void QGVR_BeginFrame();
extern void QGVR_DrawFrame(int eye);
extern void QGVR_EndFrame();
extern void QGVR_GetAudio();
extern void QGVR_KeyEvent(int state,int key,int character);
extern void QGVR_MoveEvent(float yaw, float pitch, float roll);
extern void QGVR_SetCallbacks(void *init_audio, void *write_audio);
extern void QGVR_SetResolution(int width, int height);
extern void QGVR_Analog(int enable,float x,float y);
extern void QGVR_MotionEvent(float delta, float dx, float dy);
extern int main (int argc, char **argv);
extern	int			key_consoleactive;

static JavaVM *jVM;
static jobject audioBuffer=0;
static jobject qgvrCallbackObj=0;

jmethodID android_initAudio;
jmethodID android_writeAudio;
jmethodID android_pauseAudio;
jmethodID android_resumeAudio;
jmethodID android_terminateAudio;

void jni_initAudio(void *buffer, int size)
{
	ALOGV("Calling: jni_initAudio");
    JNIEnv *env;
    jobject tmp;
    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    tmp = (*env)->NewDirectByteBuffer(env, buffer, size);
    audioBuffer = (jobject)(*env)->NewGlobalRef(env, tmp);
    return (*env)->CallVoidMethod(env, qgvrCallbackObj, android_initAudio, size);
}

void jni_writeAudio(int offset, int length)
{
	ALOGV("Calling: jni_writeAudio");
	if (audioBuffer==0) return;
    JNIEnv *env;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
    	(*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }
    (*env)->CallVoidMethod(env, qgvrCallbackObj, android_writeAudio, audioBuffer, offset, length);
}

void jni_pauseAudio()
{
	ALOGV("Calling: jni_pauseAudio");
	if (audioBuffer==0) return;
    JNIEnv *env;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
    	(*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }
    (*env)->CallVoidMethod(env, qgvrCallbackObj, android_pauseAudio);
}

void jni_resumeAudio()
{
	ALOGV("Calling: jni_resumeAudio");
	if (audioBuffer==0) return;
    JNIEnv *env;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
    	(*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }
    (*env)->CallVoidMethod(env, qgvrCallbackObj, android_resumeAudio);
}

void jni_terminateAudio()
{
	ALOGV("Calling: jni_terminateAudio");
	if (audioBuffer==0) return;
    JNIEnv *env;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
    	(*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }
    (*env)->CallVoidMethod(env, qgvrCallbackObj, android_terminateAudio);
}

//Timing stuff for joypad control
long oldtime=0;
long delta=0;
float last_joystick_x=0;
float last_joystick_y=0;

int curtime;
int Sys_Milliseconds (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int		secbase;

	gettimeofday(&tp, &tzp);

	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - secbase)*1000 + tp.tv_usec/1000;

	return curtime;
}

static const float meters_to_units = 40.0f;

//Should get this from OVR
float GVR_GetSeparation()
{
	static float separation = 0.0;
	if (separation == 0.0)
	{
		//Generic eye separation of 0.065 metres
		separation = meters_to_units * 0.065;
	}
	return separation;
}


int returnvalue = -1;
void GVR_exit(int exitCode)
{
	returnvalue = exitCode;
}

vec3_t hmdorientation;

int bigScreen = 1;

static void UnEscapeQuotes( char *arg )
{
	char *last = NULL;
	while( *arg ) {
		if( *arg == '"' && *last == '\\' ) {
			char *c_curr = arg;
			char *c_last = last;
			while( *c_curr ) {
				*c_last = *c_curr;
				c_last = c_curr;
				c_curr++;
			}
			*c_last = '\0';
		}
		last = arg;
		arg++;
	}
}

static int ParseCommandLine(char *cmdline, char **argv)
{
	char *bufp;
	char *lastp = NULL;
	int argc, last_argc;
	argc = last_argc = 0;
	for ( bufp = cmdline; *bufp; ) {
		while ( isspace(*bufp) ) {
			++bufp;
		}
		if ( *bufp == '"' ) {
			++bufp;
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ( *bufp != '"' || *lastp == '\\' ) ) {
				lastp = bufp;
				++bufp;
			}
		} else {
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ! isspace(*bufp) ) {
				++bufp;
			}
		}
		if ( *bufp ) {
			if ( argv ) {
				*bufp = '\0';
			}
			++bufp;
		}
		if( argv && last_argc != argc ) {
			UnEscapeQuotes( argv[last_argc] );
		}
		last_argc = argc;
	}
	if ( argv ) {
		argv[argc] = NULL;
	}
	return(argc);
}

/*
================================================================================

OpenGL-ES Utility Functions

================================================================================
*/

#if 0
	#define GL( func )		func; EglCheckErrors();
#else
	#define GL( func )		func;
#endif

static const char * EglErrorString( const EGLint error )
{
	switch ( error )
	{
		case EGL_SUCCESS:				return "EGL_SUCCESS";
		case EGL_NOT_INITIALIZED:		return "EGL_NOT_INITIALIZED";
		case EGL_BAD_ACCESS:			return "EGL_BAD_ACCESS";
		case EGL_BAD_ALLOC:				return "EGL_BAD_ALLOC";
		case EGL_BAD_ATTRIBUTE:			return "EGL_BAD_ATTRIBUTE";
		case EGL_BAD_CONTEXT:			return "EGL_BAD_CONTEXT";
		case EGL_BAD_CONFIG:			return "EGL_BAD_CONFIG";
		case EGL_BAD_CURRENT_SURFACE:	return "EGL_BAD_CURRENT_SURFACE";
		case EGL_BAD_DISPLAY:			return "EGL_BAD_DISPLAY";
		case EGL_BAD_SURFACE:			return "EGL_BAD_SURFACE";
		case EGL_BAD_MATCH:				return "EGL_BAD_MATCH";
		case EGL_BAD_PARAMETER:			return "EGL_BAD_PARAMETER";
		case EGL_BAD_NATIVE_PIXMAP:		return "EGL_BAD_NATIVE_PIXMAP";
		case EGL_BAD_NATIVE_WINDOW:		return "EGL_BAD_NATIVE_WINDOW";
		case EGL_CONTEXT_LOST:			return "EGL_CONTEXT_LOST";
		default:						return "unknown";
	}
}

static const char * EglFrameBufferStatusString( GLenum status )
{
	switch ( status )
	{
		case GL_FRAMEBUFFER_UNDEFINED:						return "GL_FRAMEBUFFER_UNDEFINED";
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:			return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:	return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
		case GL_FRAMEBUFFER_UNSUPPORTED:					return "GL_FRAMEBUFFER_UNSUPPORTED";
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:			return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
		default:											return "unknown";
	}
}

static void EglCheckErrors()
{
	for ( int i = 0; i < 10; i++ )
	{
		const EGLint error = eglGetError();
		if ( error == EGL_SUCCESS )
		{
			break;
		}
		ALOGE( "EGL error: %s", EglErrorString( error ) );
	}
}

/*
================================================================================

ovrEgl

================================================================================
*/

typedef struct
{
	EGLint		MajorVersion;
	EGLint		MinorVersion;
	EGLDisplay	Display;
	EGLConfig	Config;
	EGLSurface	TinySurface;
	EGLSurface	MainSurface;
	EGLContext	Context;
} ovrEgl;

static void ovrEgl_Clear( ovrEgl * egl )
{
	egl->MajorVersion = 0;
	egl->MinorVersion = 0;
	egl->Display = 0;
	egl->Config = 0;
	egl->TinySurface = EGL_NO_SURFACE;
	egl->MainSurface = EGL_NO_SURFACE;
	egl->Context = EGL_NO_CONTEXT;
}

static void ovrEgl_CreateContext( ovrEgl * egl, const ovrEgl * shareEgl )
{
	if ( egl->Display != 0 )
	{
		return;
	}

	egl->Display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
	ALOGV( "        eglInitialize( Display, &MajorVersion, &MinorVersion )" );
	eglInitialize( egl->Display, &egl->MajorVersion, &egl->MinorVersion );
	// Do NOT use eglChooseConfig, because the Android EGL code pushes in multisample
	// flags in eglChooseConfig if the user has selected the "force 4x MSAA" option in
	// settings, and that is completely wasted for our warp target.
	const int MAX_CONFIGS = 1024;
	EGLConfig configs[MAX_CONFIGS];
	EGLint numConfigs = 0;
	if ( eglGetConfigs( egl->Display, configs, MAX_CONFIGS, &numConfigs ) == EGL_FALSE )
	{
		ALOGE( "        eglGetConfigs() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	const EGLint configAttribs[] =
	{
		EGL_BLUE_SIZE,  8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE,   8,
		EGL_DEPTH_SIZE, 0,
		EGL_SAMPLES,	0,
		EGL_NONE
	};
	egl->Config = 0;
	for ( int i = 0; i < numConfigs; i++ )
	{
		EGLint value = 0;

		eglGetConfigAttrib( egl->Display, configs[i], EGL_RENDERABLE_TYPE, &value );
		if ( ( value & EGL_OPENGL_ES3_BIT_KHR ) != EGL_OPENGL_ES3_BIT_KHR )
		{
			continue;
		}

		// The pbuffer config also needs to be compatible with normal window rendering
		// so it can share textures with the window context.
		eglGetConfigAttrib( egl->Display, configs[i], EGL_SURFACE_TYPE, &value );
		if ( ( value & ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) ) != ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) )
		{
			continue;
		}

		int	j = 0;
		for ( ; configAttribs[j] != EGL_NONE; j += 2 )
		{
			eglGetConfigAttrib( egl->Display, configs[i], configAttribs[j], &value );
			if ( value != configAttribs[j + 1] )
			{
				break;
			}
		}
		if ( configAttribs[j] == EGL_NONE )
		{
			egl->Config = configs[i];
			break;
		}
	}
	if ( egl->Config == 0 )
	{
		ALOGE( "        eglChooseConfig() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	EGLint contextAttribs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	ALOGV( "        Context = eglCreateContext( Display, Config, EGL_NO_CONTEXT, contextAttribs )" );
	egl->Context = eglCreateContext( egl->Display, egl->Config, ( shareEgl != NULL ) ? shareEgl->Context : EGL_NO_CONTEXT, contextAttribs );
	if ( egl->Context == EGL_NO_CONTEXT )
	{
		ALOGE( "        eglCreateContext() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	const EGLint surfaceAttribs[] =
	{
		EGL_WIDTH, 16,
		EGL_HEIGHT, 16,
		EGL_NONE
	};
	ALOGV( "        TinySurface = eglCreatePbufferSurface( Display, Config, surfaceAttribs )" );
	egl->TinySurface = eglCreatePbufferSurface( egl->Display, egl->Config, surfaceAttribs );
	if ( egl->TinySurface == EGL_NO_SURFACE )
	{
		ALOGE( "        eglCreatePbufferSurface() failed: %s", EglErrorString( eglGetError() ) );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
	ALOGV( "        eglMakeCurrent( Display, TinySurface, TinySurface, Context )" );
	if ( eglMakeCurrent( egl->Display, egl->TinySurface, egl->TinySurface, egl->Context ) == EGL_FALSE )
	{
		ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		eglDestroySurface( egl->Display, egl->TinySurface );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
}

static void ovrEgl_DestroyContext( ovrEgl * egl )
{
	if ( egl->Display != 0 )
	{
		ALOGE( "        eglMakeCurrent( Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT )" );
		if ( eglMakeCurrent( egl->Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) == EGL_FALSE )
		{
			ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		}
	}
	if ( egl->Context != EGL_NO_CONTEXT )
	{
		ALOGE( "        eglDestroyContext( Display, Context )" );
		if ( eglDestroyContext( egl->Display, egl->Context ) == EGL_FALSE )
		{
			ALOGE( "        eglDestroyContext() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->Context = EGL_NO_CONTEXT;
	}
	if ( egl->TinySurface != EGL_NO_SURFACE )
	{
		ALOGE( "        eglDestroySurface( Display, TinySurface )" );
		if ( eglDestroySurface( egl->Display, egl->TinySurface ) == EGL_FALSE )
		{
			ALOGE( "        eglDestroySurface() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->TinySurface = EGL_NO_SURFACE;
	}
	if ( egl->Display != 0 )
	{
		ALOGE( "        eglTerminate( Display )" );
		if ( eglTerminate( egl->Display ) == EGL_FALSE )
		{
			ALOGE( "        eglTerminate() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->Display = 0;
	}
}

static void ovrEgl_CreateSurface( ovrEgl * egl, ANativeWindow * nativeWindow )
{
	if ( egl->MainSurface != EGL_NO_SURFACE )
	{
		return;
	}
	ALOGV( "        MainSurface = eglCreateWindowSurface( Display, Config, nativeWindow, attribs )" );
	const EGLint surfaceAttribs[] = { EGL_NONE };
	egl->MainSurface = eglCreateWindowSurface( egl->Display, egl->Config, nativeWindow, surfaceAttribs );
	if ( egl->MainSurface == EGL_NO_SURFACE )
	{
		ALOGE( "        eglCreateWindowSurface() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	ALOGV( "        eglMakeCurrent( display, MainSurface, MainSurface, Context )" );
	if ( eglMakeCurrent( egl->Display, egl->MainSurface, egl->MainSurface, egl->Context ) == EGL_FALSE )
	{
		ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
}

static void ovrEgl_DestroySurface( ovrEgl * egl )
{
	if ( egl->Context != EGL_NO_CONTEXT )
	{
		ALOGV( "        eglMakeCurrent( display, TinySurface, TinySurface, Context )" );
		if ( eglMakeCurrent( egl->Display, egl->TinySurface, egl->TinySurface, egl->Context ) == EGL_FALSE )
		{
			ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		}
	}
	if ( egl->MainSurface != EGL_NO_SURFACE )
	{
		ALOGV( "        eglDestroySurface( Display, MainSurface )" );
		if ( eglDestroySurface( egl->Display, egl->MainSurface ) == EGL_FALSE )
		{
			ALOGE( "        eglDestroySurface() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->MainSurface = EGL_NO_SURFACE;
	}
}

/*
================================================================================

ovrRenderTexture

================================================================================
*/

typedef struct
{
	int		Width;
	int		Height;
	int		Multisamples;
	GLuint	ColorTexture;
	GLuint	DepthBuffer;
	GLuint	FrameBuffer;
} ovrRenderTexture;

static void ovrRenderTexture_Clear( ovrRenderTexture * renderTexture )
{
	renderTexture->Width = 0;
	renderTexture->Height = 0;
	renderTexture->Multisamples = 0;
	renderTexture->ColorTexture = 0;
	renderTexture->DepthBuffer = 0;
	renderTexture->FrameBuffer = 0;
}

static bool ovrRenderTexture_Create( ovrRenderTexture * renderTexture, const int width, const int height, const int multisamples )
{
	renderTexture->Width = width;
	renderTexture->Height = height;
	renderTexture->Multisamples = multisamples;

	// Create the color buffer texture.
	GL( glGenTextures( 1, &renderTexture->ColorTexture ) );
	GL( glBindTexture( GL_TEXTURE_2D, renderTexture->ColorTexture ) );
	GL( glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL ) );
	GL( glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ) );
	GL( glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ) );
	GL( glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ) );
	GL( glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR ) );
	GL( glBindTexture( GL_TEXTURE_2D, 0 ) );

	// Create depth buffer.
	GL( glGenRenderbuffers( 1, &renderTexture->DepthBuffer ) );
	GL( glBindRenderbuffer( GL_RENDERBUFFER, renderTexture->DepthBuffer ) );
	GL( glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height ) );
	GL( glBindRenderbuffer( GL_RENDERBUFFER, 0 ) );

	// Create the frame buffer.
	GL( glGenFramebuffers( 1, &renderTexture->FrameBuffer ) );
	GL( glBindFramebuffer( GL_FRAMEBUFFER, renderTexture->FrameBuffer ) );
	GL( glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderTexture->DepthBuffer ) );
	GL( glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTexture->ColorTexture, 0 ) );
	GL( GLenum renderFramebufferStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER ) );
	GL( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
	if ( renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ALOGE( "Incomplete frame buffer object: %s", EglFrameBufferStatusString( renderFramebufferStatus ) );
		return false;
	}

	return true;
}

static void ovrRenderTexture_Destroy( ovrRenderTexture * renderTexture )
{
	GL( glDeleteFramebuffers( 1, &renderTexture->FrameBuffer ) );
	GL( glDeleteRenderbuffers( 1, &renderTexture->DepthBuffer ) );
	GL( glDeleteTextures( 1, &renderTexture->ColorTexture ) );

	renderTexture->ColorTexture = 0;
	renderTexture->DepthBuffer = 0;
	renderTexture->FrameBuffer = 0;
}

static void ovrRenderTexture_SetCurrent( ovrRenderTexture * renderTexture )
{
	GL( glBindFramebuffer( GL_FRAMEBUFFER, renderTexture->FrameBuffer ) );
}

static void ovrRenderTexture_SetNone()
{
	GL( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
}

static void ovrRenderTexture_Resolve( ovrRenderTexture * renderTexture )
{
	// Discard the depth buffer, so the tiler won't need to write it back out to memory.
	const GLenum depthAttachment[1] = { GL_DEPTH_ATTACHMENT };
	glInvalidateFramebuffer( GL_FRAMEBUFFER, 1, depthAttachment );

	// Flush this frame worth of commands.
	glFlush();
}


/*
================================================================================

ovrRenderer

================================================================================
*/

#define NUM_EYES			VRAPI_FRAME_LAYER_EYE_MAX
#define NUM_BUFFERS			3
#define NUM_MULTI_SAMPLES	1

typedef struct
{
	ovrRenderTexture	RenderTextures[NUM_BUFFERS][NUM_EYES];
	ovrRenderTexture	QuakeRenderTexture;
	int					BufferIndex;
	ovrMatrix4f			ProjectionMatrix;
	ovrMatrix4f			TanAngleMatrix;
} ovrRenderer;

static void ovrRenderer_Clear( ovrRenderer * renderer )
{
	for ( int i = 0; i < NUM_BUFFERS; i++ )
	{
		for ( int eye = 0; eye < NUM_EYES; eye++ )
		{
			ovrRenderTexture_Clear( &renderer->RenderTextures[i][eye] );
		}
	}
	ovrRenderTexture_Clear( &renderer->QuakeRenderTexture );
	renderer->BufferIndex = 0;
}

static const char VERTEX_SHADER[] =
		"uniform mat4 u_MVPMatrix;"
		"attribute vec4 a_Position;"
		"attribute vec2 a_texCoord;"
		"varying vec2 v_texCoord;"
		"void main() {"
		"  gl_Position = u_MVPMatrix * a_Position;"
		"  v_texCoord = a_texCoord;"
		"}";


static const char FRAGMENT_SHADER[] =
		"precision mediump float;"
		"varying vec2 v_texCoord;"
		"uniform sampler2D s_texture;"
		"void main() {"
		"  gl_FragColor = texture2D( s_texture, v_texCoord );"
		"}";

int loadShader(int type, const char * shaderCode){
	int shader = glCreateShader(type);
	GLint length = 0;
	GL( glShaderSource(shader, 1, &shaderCode, 0));
	GL( glCompileShader(shader));
	return shader;
}

static short indices[6] = {0, 1, 2, 0, 2, 3};

static float uvs[8] = {
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f,
		1.0f, 1.0f
};

static float SCREEN_COORDS[12] = {
		-5.0f, 4.0f, 1.0f,
		-5.0f, -4.0f, 1.0f,
		5.0f, -4.0f, 1.0f,
		5.0f, 4.0f, 1.0f
};

int positionParam = 0;
int texCoordParam = 0;
int samplerParam = 0;
int modelViewProjectionParam = 0;

int sp_Image = 0;

ovrMatrix4f modelScreen;

static void ovrRenderer_Create( ovrRenderer * renderer, const ovrHmdInfo * hmdInfo )
{
	 // Create the shaders, images
	 int vertexShader = loadShader(GL_VERTEX_SHADER, VERTEX_SHADER);
	 int fragmentShader = loadShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);

	 sp_Image = glCreateProgram();             // create empty OpenGL ES Program
	 GL( glAttachShader(sp_Image, vertexShader));   // add the vertex shader to program
	 GL( glAttachShader(sp_Image, fragmentShader)); // add the fragment shader to program
	 GL( glLinkProgram(sp_Image));                  // creates OpenGL ES program executable

	positionParam = GL( glGetAttribLocation(sp_Image, "a_Position"));
	texCoordParam = GL( glGetAttribLocation(sp_Image, "a_texCoord"));
	modelViewProjectionParam = GL( glGetUniformLocation(sp_Image, "u_MVPMatrix"));
	samplerParam = GL( glGetUniformLocation(sp_Image, "s_texture"));

    modelScreen = ovrMatrix4f_CreateIdentity();
    ovrMatrix4f translation = ovrMatrix4f_CreateTranslation( 0, 0, -9.0f );
    modelScreen = ovrMatrix4f_Multiply( &modelScreen, &translation );

	// Create the render Textures.
	for ( int i = 0; i < NUM_BUFFERS; i++ )
	{
		for ( int eye = 0; eye < NUM_EYES; eye++ )
		{
			ovrRenderTexture_Create( &renderer->RenderTextures[i][eye],
									hmdInfo->SuggestedEyeResolution[0],
									hmdInfo->SuggestedEyeResolution[1],
									NUM_MULTI_SAMPLES );
		}

		ovrRenderTexture_Create( &renderer->QuakeRenderTexture,
								hmdInfo->SuggestedEyeResolution[0],
								hmdInfo->SuggestedEyeResolution[1],
								NUM_MULTI_SAMPLES );
	}
	renderer->BufferIndex = 0;

	// Setup the projection matrix.
	renderer->ProjectionMatrix = ovrMatrix4f_CreateProjectionFov(
										hmdInfo->SuggestedEyeFov[0] * ( M_PI / 180.0f ),
										hmdInfo->SuggestedEyeFov[1] * ( M_PI / 180.0f ),
										0.0f, 0.0f, 1.0f, 0.0f );
	renderer->TanAngleMatrix = ovrMatrix4f_TanAngleMatrixFromProjection( &renderer->ProjectionMatrix );
}

static void ovrRenderer_Destroy( ovrRenderer * renderer )
{
	for ( int i = 0; i < NUM_BUFFERS; i++ )
	{
		for ( int eye = 0; eye < NUM_EYES; eye++ )
		{
			ovrRenderTexture_Destroy( &renderer->RenderTextures[i][eye] );
		}
	}
	ovrRenderTexture_Destroy( &renderer->QuakeRenderTexture );
	renderer->BufferIndex = 0;
}

void QuatToYawPitchRoll(ovrQuatf q, vec3_t out) {
	float sqw = q.w*q.w;
	float sqx = q.x*q.x;
	float sqy = q.y*q.y;
	float sqz = q.z*q.z;
	float unit = sqx + sqy + sqz + sqw; // if normalised is one, otherwise is correction factor
	float test = q.x*q.y + q.z*q.w;
	if( test > 0.499*unit ) { // singularity at north pole
		out[YAW] = 2 * atan2(q.x,q.w) / (M_PI / 180.0f);
		out[ROLL] = -M_PI/2 / (M_PI / 180.0f) ;
		out[PITCH] = 0;
	}
	else if( test < -0.499*unit ) { // singularity at south pole
		out[YAW] = -2 * atan2(q.x,q.w) / (M_PI / 180.0f);
		out[ROLL] = M_PI/2 / (M_PI / 180.0f);
		out[PITCH] = 0;
	}
	else {
		out[YAW] = atan2(2*q.y*q.w-2*q.x*q.z , sqx - sqy - sqz + sqw) / (M_PI / 180.0f);
		out[ROLL] = -asin(2*test/unit) / (M_PI / 180.0f);
		out[PITCH] = -atan2(2*q.x*q.w-2*q.y*q.z , -sqx + sqy - sqz + sqw) / (M_PI / 180.0f);
	}
}

static void clearEdgeTexels( ovrRenderTexture * rt )
{
	GL( glEnable( GL_SCISSOR_TEST ) );
	GL( glViewport( 0, 0, rt->Width, rt->Height ) );

	// Explicitly clear the border texels to black because OpenGL-ES does not support GL_CLAMP_TO_BORDER.
	// Clear to fully opaque black.
	GL( glClearColor( 0.0f, 0.0f, 0.0f, 1.0f ) );

	// bottom
	GL( glScissor( 0, 0, rt->Width, 1 ) );
	GL( glClear( GL_COLOR_BUFFER_BIT ) );
	// top
	GL( glScissor( 0, rt->Height - 1, rt->Width, 1 ) );
	GL( glClear( GL_COLOR_BUFFER_BIT ) );
	// left
	GL( glScissor( 0, 0, 1, rt->Height ) );
	GL( glClear( GL_COLOR_BUFFER_BIT ) );
	// right
	GL( glScissor( rt->Width - 1, 0, 1, rt->Height ) );
	GL( glClear( GL_COLOR_BUFFER_BIT ) );

	GL( glScissor( 0, 0, 0, 0 ) );
	GL( glDisable( GL_SCISSOR_TEST ) );
}

static ovrFrameParms ovrRenderer_RenderFrame( ovrRenderer * renderer, const ovrJava * java, long long frameIndex, int minimumVsyncs,
											const ovrTracking * tracking )
{
	ovrFrameParms parms = vrapi_DefaultFrameParms( java, VRAPI_FRAME_INIT_DEFAULT, 0 );
	parms.FrameIndex = frameIndex;
	parms.MinimumVsyncs = minimumVsyncs;

	long t=Sys_Milliseconds();
	delta=t-oldtime;
	oldtime=t;
	if (delta>1000)
		delta=1000;

	QGVR_MotionEvent(delta, last_joystick_x, last_joystick_y);


	// Calculate the center view matrix.
	const ovrHeadModelParms headModelParms = vrapi_DefaultHeadModelParms();
	const ovrMatrix4f centerEyeViewMatrix = vrapi_GetCenterEyeViewMatrix( &headModelParms, tracking, NULL );

	//Get orientation
	// We extract Yaw, Pitch, Roll instead of directly using the orientation
	// to allow "additional" yaw manipulation with mouse/controller.
	const ovrQuatf quat = tracking->HeadPose.Pose.Orientation;
	QuatToYawPitchRoll(quat, hmdorientation);

	//Set move information
	QGVR_MoveEvent(hmdorientation[YAW], hmdorientation[PITCH], hmdorientation[ROLL]);

	//Set everything up
	QGVR_BeginFrame();

	// Render the eye images.
	for ( int eye = 0; eye < NUM_EYES; eye++ )
	{
		ovrRenderTexture * rt = NULL;
		if (bigScreen != 0)
			rt = &renderer->QuakeRenderTexture;
		else
			rt = &renderer->RenderTextures[renderer->BufferIndex][eye];

		ovrRenderTexture_SetCurrent( rt );

		GL( glEnable( GL_SCISSOR_TEST ) );
		GL( glDepthMask( GL_TRUE ) );
		GL( glEnable( GL_DEPTH_TEST ) );
		GL( glDepthFunc( GL_LEQUAL ) );
		GL( glViewport( 0, 0, rt->Width, rt->Height ) );
		GL( glScissor( 0, 0, rt->Width, rt->Height ) );
		GL( glClearColor( 0.0f, 0.0f, 0.0f, 1.0f ) );
		GL( glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ) );
		GL( glDisable(GL_SCISSOR_TEST));

		//Now do the drawing for this eye
		QGVR_DrawFrame(eye);

		//Clear edge to prevent smearing
		clearEdgeTexels( rt );

		if (bigScreen != 0)
		{
			rt = &renderer->RenderTextures[renderer->BufferIndex][eye];
			ovrRenderTexture_SetCurrent( rt );

			GL( glEnable( GL_SCISSOR_TEST ) );
			GL( glDepthMask( GL_TRUE ) );
			GL( glEnable( GL_DEPTH_TEST ) );
			GL( glDepthFunc( GL_LEQUAL ) );
			GL( glViewport( 0, 0, rt->Width, rt->Height ) );
			GL( glScissor( 0, 0, rt->Width, rt->Height ) );
			GL( glClearColor( 0.01f, 0.0f, 0.0f, 1.0f ) );
			GL( glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ) );
			GL( glDisable(GL_SCISSOR_TEST));

			GL( glUseProgram(sp_Image));

			// Set the position of the screen
			GL( glVertexAttribPointer(positionParam, 3, GL_FLOAT, GL_FALSE, 0, SCREEN_COORDS));

			// Prepare the texture coordinates
			GL( glVertexAttribPointer(texCoordParam, 2, GL_FLOAT, GL_FALSE, 0, uvs) );

			// Apply the projection and view transformation
			ovrMatrix4f eyeViewMatrix = vrapi_GetEyeViewMatrix( &headModelParms, &centerEyeViewMatrix, eye );
			ovrMatrix4f modelView = ovrMatrix4f_Multiply( &eyeViewMatrix, &modelScreen );
			ovrMatrix4f modelViewProjection = ovrMatrix4f_Multiply( &renderer->ProjectionMatrix, &modelView );
			GL( glUniformMatrix4fv(modelViewProjectionParam, 1, GL_TRUE, (const GLfloat *)modelViewProjection.M[0]) );

			// Bind texture to fbo's color texture
			GL( glActiveTexture(GL_TEXTURE0) );
			GL( glBindTexture(GL_TEXTURE_2D, renderer->QuakeRenderTexture.ColorTexture) );

			// Set the sampler texture unit to our fbo's color texture
			GL( glUniform1i(samplerParam, 0) );

			// Draw the triangles
			GL( glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices) );

			// Disable vertex array
			GL( glBindTexture(GL_TEXTURE_2D, 0) );

			clearEdgeTexels( rt );
		}

		ovrRenderTexture_Resolve( rt );

		parms.Layers[VRAPI_FRAME_LAYER_TYPE_WORLD].Images[eye].TexId = rt->ColorTexture;
		parms.Layers[VRAPI_FRAME_LAYER_TYPE_WORLD].Images[eye].TexCoordsFromTanAngles = renderer->TanAngleMatrix;
		parms.Layers[VRAPI_FRAME_LAYER_TYPE_WORLD].Images[eye].HeadPose = tracking->HeadPose;
	}

	QGVR_EndFrame();

	ovrRenderTexture_SetNone();

	renderer->BufferIndex = ( renderer->BufferIndex + 1 ) % NUM_BUFFERS;

	return parms;
}


/*================================================================================

ovrApp

================================================================================
*/

typedef enum
{
	BACK_BUTTON_STATE_NONE,
	BACK_BUTTON_STATE_PENDING_DOUBLE_TAP,
	BACK_BUTTON_STATE_PENDING_SHORT_PRESS,
	BACK_BUTTON_STATE_SKIP_UP
} ovrBackButtonState;

typedef struct
{
	ovrJava				Java;
	ovrEgl				Egl;
	ANativeWindow *		NativeWindow;
	bool				Resumed;
	ovrMobile *			Ovr;
	long long			FrameIndex;
	int					MinimumVsyncs;
	ovrBackButtonState	BackButtonState;
	bool				BackButtonDown;
	double				BackButtonDownStartTime;
	ovrRenderer			Renderer;
} ovrApp;

static void ovrApp_Clear( ovrApp * app )
{
	app->Java.Vm = NULL;
	app->Java.Env = NULL;
	app->Java.ActivityObject = NULL;
	app->NativeWindow = NULL;
	app->Resumed = false;
	app->Ovr = NULL;
	app->FrameIndex = 1;
	app->MinimumVsyncs = 1;
	app->BackButtonState = BACK_BUTTON_STATE_NONE;
	app->BackButtonDown = false;
	app->BackButtonDownStartTime = 0.0;

	ovrEgl_Clear( &app->Egl );
	ovrRenderer_Clear( &app->Renderer );
}

static void ovrApp_HandleVrModeChanges( ovrApp * app )
{
	if ( app->NativeWindow != NULL && app->Egl.MainSurface == EGL_NO_SURFACE )
	{
		ovrEgl_CreateSurface( &app->Egl, app->NativeWindow );
	}

	if ( app->Resumed != false && app->NativeWindow != NULL )
	{
		if ( app->Ovr == NULL )
		{
			ovrModeParms parms = vrapi_DefaultModeParms( &app->Java );
			parms.CpuLevel = CPU_LEVEL;
			parms.GpuLevel = GPU_LEVEL;
			parms.MainThreadTid = gettid();

			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );

			app->Ovr = vrapi_EnterVrMode( &parms );

			ALOGV( "        vrapi_EnterVrMode()" );
			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );
		}
	}
	else
	{
		if ( app->Ovr != NULL )
		{
			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );

			vrapi_LeaveVrMode( app->Ovr );
			app->Ovr = NULL;

			ALOGV( "        vrapi_LeaveVrMode()" );
			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );
		}
	}

	if ( app->NativeWindow == NULL && app->Egl.MainSurface != EGL_NO_SURFACE )
	{
		ovrEgl_DestroySurface( &app->Egl );
	}
}

static void ovrApp_BackButtonAction( ovrApp * app )
{
	if ( app->BackButtonState == BACK_BUTTON_STATE_PENDING_DOUBLE_TAP )
	{
		ALOGV( "back button double tap" );
		app->BackButtonState = BACK_BUTTON_STATE_SKIP_UP;
	}
	else if ( app->BackButtonState == BACK_BUTTON_STATE_PENDING_SHORT_PRESS && !app->BackButtonDown )
	{
		if ( ( vrapi_GetTimeInSeconds() - app->BackButtonDownStartTime ) > BACK_BUTTON_DOUBLE_TAP_TIME_IN_SECONDS )
		{
			ALOGV( "back button short press" );
			ALOGV( "        ovr_StartSystemActivity( %s )", PUI_CONFIRM_QUIT );
			ovr_StartSystemActivity( &app->Java, PUI_CONFIRM_QUIT, NULL );
			app->BackButtonState = BACK_BUTTON_STATE_NONE;
		}
	}
	else if ( app->BackButtonState == BACK_BUTTON_STATE_NONE && app->BackButtonDown )
	{
		if ( ( vrapi_GetTimeInSeconds() - app->BackButtonDownStartTime ) > BACK_BUTTON_LONG_PRESS_TIME_IN_SECONDS )
		{
			ALOGV( "back button long press" );
			ALOGV( "        ovr_StartSystemActivity( %s )", PUI_GLOBAL_MENU );
			ovr_StartSystemActivity( &app->Java, PUI_GLOBAL_MENU, NULL );
			app->BackButtonState = BACK_BUTTON_STATE_SKIP_UP;
		}
	}
}

static int ovrApp_HandleKeyEvent( ovrApp * app, const int keyCode, const int action, const int character )
{
	// Handle GearVR back button.
	if ( keyCode == AKEYCODE_BACK )
	{
		if ( action == AKEY_EVENT_ACTION_DOWN )
		{
			if ( !app->BackButtonDown )
			{
				if ( ( vrapi_GetTimeInSeconds() - app->BackButtonDownStartTime ) < BACK_BUTTON_DOUBLE_TAP_TIME_IN_SECONDS )
				{
					app->BackButtonState = BACK_BUTTON_STATE_PENDING_DOUBLE_TAP;
				}
				app->BackButtonDownStartTime = vrapi_GetTimeInSeconds();
			}
			app->BackButtonDown = true;
		}
		else if ( action == AKEY_EVENT_ACTION_UP )
		{
			if ( app->BackButtonState == BACK_BUTTON_STATE_NONE )
			{
				if ( ( vrapi_GetTimeInSeconds() - app->BackButtonDownStartTime ) < BACK_BUTTON_SHORT_PRESS_TIME_IN_SECONDS )
				{
					app->BackButtonState = BACK_BUTTON_STATE_PENDING_SHORT_PRESS;
				}
			}
			else if ( app->BackButtonState == BACK_BUTTON_STATE_SKIP_UP )
			{
				app->BackButtonState = BACK_BUTTON_STATE_NONE;
			}
			app->BackButtonDown = false;
		}
		return 1;
	}
	else
	{
		//Dispatch to quake
		QGVR_KeyEvent(action == AKEY_EVENT_ACTION_DOWN ? 1 : 0, keyCode, character);
	}

	return 0;
}

#define SOURCE_GAMEPAD 	0x00000401
#define SOURCE_JOYSTICK 0x01000010
static int ovrApp_HandleTouchEvent( ovrApp * app, const int source, const int action, const float x, const float y )
{
	// Handle GearVR touch pad.
	if ( app->Ovr != NULL )
	{
		if (source == SOURCE_JOYSTICK || source == SOURCE_GAMEPAD)
			QGVR_Analog(true, x, y);
	}
	return 1;
}

static int ovrApp_HandleMotionEvent( ovrApp * app, const int source, const int action, const float x, const float y )
{
	static bool down = false;
	if (source == SOURCE_JOYSTICK || source == SOURCE_GAMEPAD)
	{
		last_joystick_x=x;
		last_joystick_y=y;
	}
	return 1;
}

static void ovrApp_HandleSystemEvents( ovrApp * app )
{
	// Handle any pending system activity events.
	size_t const MAX_EVENT_SIZE = 4096;
	char eventBuffer[MAX_EVENT_SIZE];

	for ( eVrApiEventStatus status = ovr_GetNextPendingEvent( eventBuffer, MAX_EVENT_SIZE ); 
			status >= VRAPI_EVENT_PENDING; 
				status = ovr_GetNextPendingEvent( eventBuffer, MAX_EVENT_SIZE ) )
	{
		if ( status != VRAPI_EVENT_PENDING )
		{
			if ( status != VRAPI_EVENT_CONSUMED )
			{
				ALOGE( "Error %i handing System Activities Event", status );
			}
			continue;
		}

		// parse JSON and handle event
	}
}

/*
================================================================================

ovrMessageQueue

================================================================================
*/

typedef enum
{
	MQ_WAIT_NONE,		// don't wait
	MQ_WAIT_RECEIVED,	// wait until the consumer thread has received the message
	MQ_WAIT_PROCESSED	// wait until the consumer thread has processed the message
} ovrMQWait;

#define MAX_MESSAGE_PARMS	8
#define MAX_MESSAGES		1024

typedef struct
{
	int			Id;
	ovrMQWait	Wait;
	long long	Parms[MAX_MESSAGE_PARMS];
} ovrMessage;

static void ovrMessage_Init( ovrMessage * message, const int id, const int wait )
{
	message->Id = id;
	message->Wait = wait;
	memset( message->Parms, 0, sizeof( message->Parms ) );
}

static void		ovrMessage_SetPointerParm( ovrMessage * message, int index, void * ptr ) { *(void **)&message->Parms[index] = ptr; }
static void *	ovrMessage_GetPointerParm( ovrMessage * message, int index ) { return *(void **)&message->Parms[index]; }
static void		ovrMessage_SetIntegerParm( ovrMessage * message, int index, int value ) { message->Parms[index] = value; }
static int		ovrMessage_GetIntegerParm( ovrMessage * message, int index ) { return (int)message->Parms[index]; }
static void		ovrMessage_SetFloatParm( ovrMessage * message, int index, float value ) { *(float *)&message->Parms[index] = value; }
static float	ovrMessage_GetFloatParm( ovrMessage * message, int index ) { return *(float *)&message->Parms[index]; }

// Cyclic queue with messages.
typedef struct
{
	ovrMessage	 		Messages[MAX_MESSAGES];
	volatile int		Head;	// dequeue at the head
	volatile int		Tail;	// enqueue at the tail
	volatile bool		Enabled;
	ovrMQWait			Wait;
	pthread_mutex_t		Mutex;
	pthread_cond_t		Posted;
	pthread_cond_t		Received;
	pthread_cond_t		Processed;
} ovrMessageQueue;

static void ovrMessageQueue_Create( ovrMessageQueue * messageQueue )
{
	messageQueue->Head = 0;
	messageQueue->Tail = 0;
	messageQueue->Enabled = false;
	messageQueue->Wait = MQ_WAIT_NONE;

	pthread_mutexattr_t	attr;
	pthread_mutexattr_init( &attr );
	pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
	pthread_mutex_init( &messageQueue->Mutex, &attr );
	pthread_mutexattr_destroy( &attr );
	pthread_cond_init( &messageQueue->Posted, NULL );
	pthread_cond_init( &messageQueue->Received, NULL );
	pthread_cond_init( &messageQueue->Processed, NULL );
}

static void ovrMessageQueue_Destroy( ovrMessageQueue * messageQueue )
{
	pthread_mutex_destroy( &messageQueue->Mutex );
	pthread_cond_destroy( &messageQueue->Posted );
	pthread_cond_destroy( &messageQueue->Received );
	pthread_cond_destroy( &messageQueue->Processed );
}

static void ovrMessageQueue_Enable( ovrMessageQueue * messageQueue, const bool set )
{
	messageQueue->Enabled = set;
}

static void ovrMessageQueue_PostMessage( ovrMessageQueue * messageQueue, const ovrMessage * message )
{
	if ( !messageQueue->Enabled )
	{
		return;
	}
	while ( messageQueue->Tail - messageQueue->Head >= MAX_MESSAGES )
	{
		usleep( 1000 );
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	messageQueue->Messages[messageQueue->Tail & ( MAX_MESSAGES - 1 )] = *message;
	messageQueue->Tail++;
	pthread_cond_broadcast( &messageQueue->Posted );
	if ( message->Wait == MQ_WAIT_RECEIVED )
	{
		pthread_cond_wait( &messageQueue->Received, &messageQueue->Mutex );
	}
	else if ( message->Wait == MQ_WAIT_PROCESSED )
	{
		pthread_cond_wait( &messageQueue->Processed, &messageQueue->Mutex );
	}
	pthread_mutex_unlock( &messageQueue->Mutex );
}

static void ovrMessageQueue_SleepUntilMessage( ovrMessageQueue * messageQueue )
{
	if ( messageQueue->Wait == MQ_WAIT_PROCESSED )
	{
		pthread_cond_broadcast( &messageQueue->Processed );
		messageQueue->Wait = MQ_WAIT_NONE;
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	if ( messageQueue->Tail > messageQueue->Head )
	{
		pthread_mutex_unlock( &messageQueue->Mutex );
		return;
	}
	pthread_cond_wait( &messageQueue->Posted, &messageQueue->Mutex );
	pthread_mutex_unlock( &messageQueue->Mutex );
}

static bool ovrMessageQueue_GetNextMessage( ovrMessageQueue * messageQueue, ovrMessage * message, bool waitForMessages )
{
	if ( messageQueue->Wait == MQ_WAIT_PROCESSED )
	{
		pthread_cond_broadcast( &messageQueue->Processed );
		messageQueue->Wait = MQ_WAIT_NONE;
	}
	if ( waitForMessages )
	{
		ovrMessageQueue_SleepUntilMessage( messageQueue );
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	if ( messageQueue->Tail <= messageQueue->Head )
	{
		pthread_mutex_unlock( &messageQueue->Mutex );
		return false;
	}
	*message = messageQueue->Messages[messageQueue->Head & ( MAX_MESSAGES - 1 )];
	messageQueue->Head++;
	pthread_mutex_unlock( &messageQueue->Mutex );
	if ( message->Wait == MQ_WAIT_RECEIVED )
	{
		pthread_cond_broadcast( &messageQueue->Received );
	}
	else if ( message->Wait == MQ_WAIT_PROCESSED )
	{
		messageQueue->Wait = MQ_WAIT_PROCESSED;
	}
	return true;
}

/*
================================================================================

ovrAppThread

================================================================================
*/

enum
{
	MESSAGE_ON_CREATE,
	MESSAGE_ON_START,
	MESSAGE_ON_RESUME,
	MESSAGE_ON_PAUSE,
	MESSAGE_ON_STOP,
	MESSAGE_ON_DESTROY,
	MESSAGE_ON_SURFACE_CREATED,
	MESSAGE_ON_SURFACE_DESTROYED,
	MESSAGE_ON_KEY_EVENT,
	MESSAGE_ON_TOUCH_EVENT,
	MESSAGE_ON_MOTION_EVENT
};

typedef struct
{
	JavaVM *		JavaVm;
	jobject			ActivityObject;
	pthread_t		Thread;
	ovrMessageQueue	MessageQueue;
	ANativeWindow * NativeWindow;
} ovrAppThread;

int JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv *env;
    jVM = vm;
    if((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
    {
        ALOGE("Failed JNI_OnLoad");
        return -1;
    }

    return JNI_VERSION_1_4;
}

void BigScreenMode(int mode)
{
	if (bigScreen != 2)
	{
		if (key_consoleactive > 0)
			bigScreen = 1;
		else
			bigScreen = mode;
	}
}


void * AppThreadFunction( void * parm )
{
	ovrAppThread * appThread = (ovrAppThread *)parm;

	static ovrJava java;
	java.Vm = appThread->JavaVm;
	(*java.Vm)->AttachCurrentThread( java.Vm, &java.Env, NULL );
	java.ActivityObject = appThread->ActivityObject;

	ALOGV( "    vrapi_Initialize" );
	const ovrInitParms initParms = vrapi_DefaultInitParms( &java );
	vrapi_Initialize( &initParms );

	ovrApp appState;
	ovrApp_Clear( &appState );
	appState.Java = java;

	ovrEgl_CreateContext( &appState.Egl, NULL );

	const ovrHmdInfo hmdInfo = vrapi_GetHmdInfo( &appState.Java );
	ovrRenderer_Create( &appState.Renderer, &hmdInfo );

	//Always use this folder
	chdir("/sdcard/QGVR");

	bool quake_initialised = false;

	for ( bool destroyed = false; destroyed == false; )
	{
		for ( ; ; )
		{
			ovrMessage message;
			const bool waitForMessages = ( appState.Ovr == NULL && destroyed == false );
			if ( !ovrMessageQueue_GetNextMessage( &appThread->MessageQueue, &message, waitForMessages ) )
			{
				break;
			}

			switch ( message.Id )
			{
				case MESSAGE_ON_CREATE:
				{
					ALOGV( "    Initialising Quake Engine" );
					QGVR_SetResolution(appState.Renderer.QuakeRenderTexture.Width,
							appState.Renderer.QuakeRenderTexture.Height);
					char *arg = (char*)ovrMessage_GetPointerParm( &message, 0 );
					if (arg)
					{
						ALOGV("Command line %s", arg);
					    char **argv;
					    int argc=0;
						argv = malloc(sizeof(char*) * 255);
						argc = ParseCommandLine(strdup(arg), argv);
						main(argc, argv);
						quake_initialised = true;
					}
					else
					{
						int argc =1; char *argv[] = { "quake" };
						main(argc, argv);
					}
					//Ensure game starts with credits active
					MR_ToggleMenu(2);
					break;
				}
				case MESSAGE_ON_START:
				{
					returnvalue = -1;
					break;
				}
				case MESSAGE_ON_RESUME:
				{
					//If we get here, then user has opted not to quit
					jni_resumeAudio();
					appState.Resumed = true;
					break;
				}
				case MESSAGE_ON_PAUSE:
				{
					jni_pauseAudio();
					appState.Resumed = false;
					break;
				}
				case MESSAGE_ON_STOP:
				{
					break;
				}
				case MESSAGE_ON_DESTROY:
				{
					Host_Shutdown();
					jni_terminateAudio();
					appState.NativeWindow = NULL;
					destroyed = true;
					break;
				}
				case MESSAGE_ON_SURFACE_CREATED:	{ appState.NativeWindow = (ANativeWindow *)ovrMessage_GetPointerParm( &message, 0 ); break; }
				case MESSAGE_ON_SURFACE_DESTROYED:	{ appState.NativeWindow = NULL; break; }
				case MESSAGE_ON_KEY_EVENT:			{ ovrApp_HandleKeyEvent( &appState,
														ovrMessage_GetIntegerParm( &message, 0 ),
														ovrMessage_GetIntegerParm( &message, 1 ),
														ovrMessage_GetIntegerParm( &message, 2 )); break; }
				case MESSAGE_ON_TOUCH_EVENT:		{ ovrApp_HandleTouchEvent( &appState,
						ovrMessage_GetIntegerParm( &message, 0 ),
						ovrMessage_GetIntegerParm( &message, 1 ),
						ovrMessage_GetFloatParm( &message, 2 ),
						ovrMessage_GetFloatParm( &message, 3 ) ); break; }
				case MESSAGE_ON_MOTION_EVENT:		{ ovrApp_HandleMotionEvent( &appState,
						ovrMessage_GetIntegerParm( &message, 0 ),
						ovrMessage_GetIntegerParm( &message, 1 ),
						ovrMessage_GetFloatParm( &message, 2 ),
						ovrMessage_GetFloatParm( &message, 3 ) ); break; }
			}

			ovrApp_HandleVrModeChanges( &appState );
		}

		ovrApp_BackButtonAction( &appState );
		ovrApp_HandleSystemEvents( &appState );

		if ( appState.Ovr == NULL )
		{
			continue;
		}

		if (!quake_initialised)
			continue;

		// This is the only place the frame index is incremented, right before
		// calling vrapi_GetPredictedDisplayTime().
		appState.FrameIndex++;

		// Get the HMD pose, predicted for the middle of the time period during which
		// the new eye images will be displayed. The number of frames predicted ahead
		// depends on the pipeline depth of the engine and the synthesis rate.
		// The better the prediction, the less black will be pulled in at the edges.
		const double predictedDisplayTime = vrapi_GetPredictedDisplayTime( appState.Ovr, appState.FrameIndex );
		const ovrTracking tracking = vrapi_GetPredictedTracking( appState.Ovr, predictedDisplayTime );

		// Render eye images and setup ovrFrameParms using ovrTracking.
		const ovrFrameParms parms = ovrRenderer_RenderFrame( &appState.Renderer, &appState.Java,
															appState.FrameIndex, appState.MinimumVsyncs,
															&tracking );

		// Hand over the eye images to the time warp.
		vrapi_SubmitFrame( appState.Ovr, &parms );

		//User is thinking about quitting
		if (returnvalue != -1)
		{
			jni_pauseAudio();
			ovr_StartSystemActivity( &appState.Java, PUI_CONFIRM_QUIT, NULL );
		}
	}

	ovrRenderer_Destroy( &appState.Renderer );

	ovrEgl_DestroyContext( &appState.Egl );
	vrapi_Shutdown();

	(*java.Vm)->DetachCurrentThread( java.Vm );
}

static void ovrAppThread_Create( ovrAppThread * appThread, JNIEnv * env, jobject activityObject, jclass activityClass)
{
	(*env)->GetJavaVM( env, &appThread->JavaVm );
	appThread->ActivityObject = (*env)->NewGlobalRef( env, activityObject );
	appThread->Thread = 0;
	appThread->NativeWindow = NULL;
	ovrMessageQueue_Create( &appThread->MessageQueue );

	const int createErr = pthread_create( &appThread->Thread, NULL, AppThreadFunction, appThread );
	if ( createErr != 0 )
	{
		ALOGE( "pthread_create returned %i", createErr );
	}
}

static void ovrAppThread_Destroy( ovrAppThread * appThread, JNIEnv * env )
{
	pthread_join( appThread->Thread, NULL );
	(*env)->DeleteGlobalRef( env, appThread->ActivityObject );
	ovrMessageQueue_Destroy( &appThread->MessageQueue );
}

/*
================================================================================

Activity lifecycle

================================================================================
*/

JNIEXPORT jlong JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onCreate( JNIEnv * env, jclass activityClass, jobject activity,
		jstring commandLineParams)
{
	ALOGV( "    GLES3JNILib::onCreate()" );

	jboolean iscopy;
    const char *arg = (*env)->GetStringUTFChars(env, commandLineParams, &iscopy);

    char *cmdLine = NULL;
    if (arg && strlen(arg))
    {
    	cmdLine = strdup(arg);
    }

	(*env)->ReleaseStringUTFChars(env, commandLineParams, arg);

	ovrAppThread * appThread = (ovrAppThread *) malloc( sizeof( ovrAppThread ) );
	ovrAppThread_Create( appThread, env, activity, activityClass );

	ovrMessageQueue_Enable( &appThread->MessageQueue, true );
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_CREATE, MQ_WAIT_PROCESSED );
	ovrMessage_SetPointerParm( &message, 0, cmdLine );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );

	return (jlong)((size_t)appThread);
}


JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_setCallbackObject(JNIEnv *env, jclass c, jobject obj)
{
    qgvrCallbackObj = obj;
    jclass qgvrCallbackClass;

    (*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4);
    qgvrCallbackObj = (jobject)(*env)->NewGlobalRef(env, obj);
    qgvrCallbackClass = (*env)->GetObjectClass(env, qgvrCallbackObj);

    android_initAudio = (*env)->GetMethodID(env,qgvrCallbackClass,"initAudio","(I)V");
    android_writeAudio = (*env)->GetMethodID(env,qgvrCallbackClass,"writeAudio","(Ljava/nio/ByteBuffer;II)V");
    android_pauseAudio = (*env)->GetMethodID(env,qgvrCallbackClass,"pauseAudio","()V");
    android_resumeAudio = (*env)->GetMethodID(env,qgvrCallbackClass,"resumeAudio","()V");
    android_terminateAudio = (*env)->GetMethodID(env,qgvrCallbackClass,"terminateAudio","()V");
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onStart( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onStart()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_START, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onResume( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onResume()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_RESUME, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onPause( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onPause()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_PAUSE, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onStop( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onStop()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_STOP, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onDestroy( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onDestroy()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_DESTROY, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
	ovrMessageQueue_Enable( &appThread->MessageQueue, false );

	ovrAppThread_Destroy( appThread, env );
	free( appThread );
}

/*
================================================================================

Surface lifecycle

================================================================================
*/

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onSurfaceCreated( JNIEnv * env, jobject obj, jlong handle, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceCreated()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);

	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}

	ALOGV( "        NativeWindow = ANativeWindow_fromSurface( env, surface )" );
	appThread->NativeWindow = newNativeWindow;
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED );
	ovrMessage_SetPointerParm( &message, 0, appThread->NativeWindow );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onSurfaceChanged( JNIEnv * env, jobject obj, jlong handle, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceChanged()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);

	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}

	if ( newNativeWindow != appThread->NativeWindow )
	{
		if ( appThread->NativeWindow != NULL )
		{
			ovrMessage message;
			ovrMessage_Init( &message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED );
			ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
			ALOGV( "        ANativeWindow_release( NativeWindow )" );
			ANativeWindow_release( appThread->NativeWindow );
			appThread->NativeWindow = NULL;
		}
		if ( newNativeWindow != NULL )
		{
			ALOGV( "        NativeWindow = ANativeWindow_fromSurface( env, surface )" );
			appThread->NativeWindow = newNativeWindow;
			ovrMessage message;
			ovrMessage_Init( &message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED );
			ovrMessage_SetPointerParm( &message, 0, appThread->NativeWindow );
			ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
		}
	}
	else if ( newNativeWindow != NULL )
	{
		ANativeWindow_release( newNativeWindow );
	}
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onSurfaceDestroyed( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onSurfaceDestroyed()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
	ALOGV( "        ANativeWindow_release( NativeWindow )" );
	ANativeWindow_release( appThread->NativeWindow );
	appThread->NativeWindow = NULL;
}

/*
================================================================================

Input

================================================================================
*/

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onKeyEvent( JNIEnv * env, jobject obj, jlong handle, int keyCode, int action, int character )
{
	if ( action == AKEY_EVENT_ACTION_UP )
	{
		ALOGV( "    GLES3JNILib::onKeyEvent( %d, %d )", keyCode, action );
	}
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_KEY_EVENT, MQ_WAIT_NONE );
	ovrMessage_SetIntegerParm( &message, 0, keyCode );
	ovrMessage_SetIntegerParm( &message, 1, action );
	ovrMessage_SetIntegerParm( &message, 2, character );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onTouchEvent( JNIEnv * env, jobject obj, jlong handle, int source, int action, float x, float y )
{
	if ( action == AMOTION_EVENT_ACTION_UP )
	{
		ALOGV( "    GLES3JNILib::onTouchEvent( %d, %d, %1.0f, %1.0f )", source, action, x, y );
	}
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_TOUCH_EVENT, MQ_WAIT_NONE );
	ovrMessage_SetIntegerParm( &message, 0, source );
	ovrMessage_SetIntegerParm( &message, 1, action );
	ovrMessage_SetFloatParm( &message, 2, x );
	ovrMessage_SetFloatParm( &message, 3, y );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_onMotionEvent( JNIEnv * env, jobject obj, jlong handle, int source, int action, float x, float y )
{
	if ( action == AMOTION_EVENT_ACTION_UP )
	{
		ALOGV( "    GLES3JNILib::onMotionEvent( %d, %d, %1.0f, %1.0f )", source, action, x, y );
	}
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_MOTION_EVENT, MQ_WAIT_NONE );
	ovrMessage_SetIntegerParm( &message, 0, source );
	ovrMessage_SetIntegerParm( &message, 1, action );
	ovrMessage_SetFloatParm( &message, 2, x );
	ovrMessage_SetFloatParm( &message, 3, y );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_quakegearvr_GLES3JNILib_requestAudioData(JNIEnv *env, jclass c, jlong handle)
{
	ALOGV("Calling: QGVR_GetAudio");
	QGVR_GetAudio();
}
