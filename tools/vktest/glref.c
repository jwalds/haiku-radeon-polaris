/*
 * glref: reference scenes for the image comparison tests. Renders fixed
 * scenes through Zink and RADV into 256x256 framebuffer objects (OpenGL ES
 * 3.1), reads them back and writes <directory>/gl-<scene>.png; the test
 * runner compares them with tests/reference/. Every scene covers a part of
 * the pipeline: vertex color interpolation, mipmapped texture filtering in
 * perspective, the depth test, blending, 4x MSAA resolve, instancing and a
 * compute shader writing an image.
 *
 * build: gcc -o glref glref.c xdg-shell-protocol.c -lEGL -lGLESv2
 *        -lwayland-client -lwayland-egl -lm
 * usage: glref [directory] [scene...]
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include "xdg-shell-client-protocol.h"

#include "png_write.h"

enum { kSize = 256 };

static struct wl_compositor *sCompositor;
static struct xdg_wm_base *sWmBase;
static int sConfigured, sClosed;


static void
WmBasePing(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = {WmBasePing};


static void
RegistryGlobal(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		sCompositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		sWmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(sWmBase, &sWmBaseListener, NULL);
	}
}

static void
RegistryRemove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener sRegistryListener = {
	RegistryGlobal, RegistryRemove};


static void
SurfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
	sConfigured = 1;
}

static const struct xdg_surface_listener sSurfaceListener = {SurfaceConfigure};


static void
ToplevelConfigure(void *data, struct xdg_toplevel *toplevel, int32_t width,
	int32_t height, struct wl_array *states)
{
}

static void
ToplevelClose(void *data, struct xdg_toplevel *toplevel)
{
	sClosed = 1;
}

static const struct xdg_toplevel_listener sToplevelListener = {
	ToplevelConfigure, ToplevelClose};



static GLuint
CompileShader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("[!] shader: %s\n", log);
		exit(1);
	}
	return shader;
}


static GLuint
Program(const char *vertex, const char *fragment)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, CompileShader(GL_VERTEX_SHADER, vertex));
	glAttachShader(program, CompileShader(GL_FRAGMENT_SHADER, fragment));
	glLinkProgram(program);
	GLint ok;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		printf("[!] link: %s\n", log);
		exit(1);
	}
	return program;
}


// a vertex shader with position (vec4) and color (vec4) attributes
static const char *kColorVertex =
	"#version 300 es\n"
	"layout(location = 0) in vec4 position;\n"
	"layout(location = 1) in vec4 color;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"	gl_Position = position;\n"
	"	vColor = color;\n"
	"}\n";

static const char *kColorFragment =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec4 vColor;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	fragColor = vColor;\n"
	"}\n";


// draws triangles from interleaved position (4) + color (4) floats
static void
DrawColored(const float *vertices, int count)
{
	static GLuint program;
	if (program == 0)
		program = Program(kColorVertex, kColorFragment);
	glUseProgram(program);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
		vertices);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
		vertices + 4);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, count);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}


static void
SceneGradient(void)
{
	static const float vertices[] = {
		-0.8f, -0.8f, 0, 1,  1, 0, 0, 1,
		 0.8f, -0.8f, 0, 1,  0, 1, 0, 1,
		 0.0f,  0.8f, 0, 1,  0, 0, 1, 1,
	};
	DrawColored(vertices, 3);
}


static void
SceneTexture(void)
{
	// a checkerboard with a red border, mipmapped
	enum { kTextureSize = 64 };
	static uint8_t texels[kTextureSize * kTextureSize * 4];
	for (int y = 0; y < kTextureSize; y++) {
		for (int x = 0; x < kTextureSize; x++) {
			uint8_t *texel = texels + (y * kTextureSize + x) * 4;
			int border = x == 0 || y == 0 || x == kTextureSize - 1
				|| y == kTextureSize - 1;
			int dark = ((x / 8) + (y / 8)) % 2;
			texel[0] = border ? 255 : dark ? 30 : 230;
			texel[1] = border ? 0 : dark ? 60 : 230;
			texel[2] = border ? 0 : dark ? 90 : 200;
			texel[3] = 255;
		}
	}
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTextureSize, kTextureSize, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, texels);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	static GLuint program;
	if (program == 0) {
		program = Program(
			"#version 300 es\n"
			"layout(location = 0) in vec4 position;\n"
			"layout(location = 1) in vec2 uv;\n"
			"out vec2 vUv;\n"
			"void main() {\n"
			"	gl_Position = position;\n"
			"	vUv = uv;\n"
			"}\n",
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform sampler2D tex;\n"
			"in vec2 vUv;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	fragColor = texture(tex, vUv);\n"
			"}\n");
	}
	// a floor rectangle in perspective (the far edge has w = 4), wider than
	// the screen at the near edge: clipped
	static const float vertices[] = {
		-4.0f, -1.0f, 0, 1,    0,  0,
		 4.0f, -1.0f, 0, 1,    8,  0,
		 4.0f,  4.0f, 0, 4,    8, 16,
		-4.0f, -1.0f, 0, 1,    0,  0,
		 4.0f,  4.0f, 0, 4,    8, 16,
		-4.0f,  4.0f, 0, 4,    0, 16,
	};
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "tex"), 0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
		vertices);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
		vertices + 4);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDeleteTextures(1, &texture);
}


static void
SceneDepth(void)
{
	// two triangles crossing each other in depth
	static const float vertices[] = {
		-0.9f, -0.6f, -0.8f, 1,  1.0f, 0.6f, 0.1f, 1,
		 0.9f, -0.6f,  0.8f, 1,  1.0f, 0.6f, 0.1f, 1,
		 0.0f,  0.9f,  0.0f, 1,  1.0f, 0.6f, 0.1f, 1,
		-0.9f,  0.6f,  0.8f, 1,  0.1f, 0.5f, 1.0f, 1,
		 0.9f,  0.6f, -0.8f, 1,  0.1f, 0.5f, 1.0f, 1,
		 0.0f, -0.9f,  0.0f, 1,  0.1f, 0.5f, 1.0f, 1,
	};
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	DrawColored(vertices, 6);
	glDisable(GL_DEPTH_TEST);
}


static void
SceneBlend(void)
{
	float vertices[3 * 6 * 8];
	static const float colors[3][4] = {
		{1, 0, 0, 0.5f}, {0, 1, 0, 0.5f}, {0, 0, 1, 0.5f}};
	static const float centers[3][2] = {
		{-0.25f, -0.2f}, {0.25f, -0.2f}, {0.0f, 0.25f}};
	static const float corners[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
	float *v = vertices;
	for (int quad = 0; quad < 3; quad++) {
		for (int i = 0; i < 6; i++) {
			*v++ = centers[quad][0] + corners[i][0] * 0.45f;
			*v++ = centers[quad][1] + corners[i][1] * 0.45f;
			*v++ = 0;
			*v++ = 1;
			memcpy(v, colors[quad], 4 * sizeof(float));
			v += 4;
		}
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	DrawColored(vertices, 18);
	glDisable(GL_BLEND);
}


static void
SceneMsaa(void)
{
	// thin triangles into a 4x multisampled renderbuffer, resolved into the
	// scene's framebuffer
	GLint target;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
	GLuint framebuffer, renderbuffer;
	glGenRenderbuffers(1, &renderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, kSize,
		kSize);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, renderbuffer);
	glClearColor(0.1f, 0.1f, 0.2f, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	float vertices[12 * 3 * 8];
	float *v = vertices;
	for (int i = 0; i < 12; i++) {
		float angle = i * 3.14159265f / 12;
		float c = cosf(angle), s = sinf(angle);
		const float points[3][2] = {{0, 0}, {0.95f, -0.04f}, {0.95f, 0.04f}};
		for (int p = 0; p < 3; p++) {
			*v++ = c * points[p][0] - s * points[p][1];
			*v++ = s * points[p][0] + c * points[p][1];
			*v++ = 0;
			*v++ = 1;
			*v++ = 1;
			*v++ = 1 - i / 12.0f;
			*v++ = i / 12.0f;
			*v++ = 1;
		}
	}
	DrawColored(vertices, 12 * 3);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
	glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, target);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteRenderbuffers(1, &renderbuffer);
}


static void
SceneInstancing(void)
{
	static GLuint program;
	if (program == 0) {
		program = Program(
			"#version 300 es\n"
			"layout(location = 0) in vec2 corner;\n"
			"out vec4 vColor;\n"
			"void main() {\n"
			"	int x = gl_InstanceID % 16, y = gl_InstanceID / 16;\n"
			"	vec2 origin = vec2(float(x), float(y)) / 8.0 - 1.0;\n"
			"	gl_Position = vec4(origin + (corner * 0.8 + 0.1) / 8.0,"
				" 0.0, 1.0);\n"
			"	vColor = vec4(float(x) / 15.0, float(y) / 15.0,\n"
			"		float((x + y) % 2), 1.0);\n"
			"}\n",
			kColorFragment);
	}
	static const float corners[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
	glUseProgram(program);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, corners);
	glEnableVertexAttribArray(0);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 256);
	glDisableVertexAttribArray(0);
}


static void
SceneCompute(GLuint colorTexture)
{
	// a compute shader writes the image directly
	static GLuint program;
	if (program == 0) {
		GLuint shader = CompileShader(GL_COMPUTE_SHADER,
			"#version 310 es\n"
			"layout(local_size_x = 8, local_size_y = 8) in;\n"
			"layout(rgba8, binding = 0) writeonly uniform highp image2D image;\n"
			"void main() {\n"
			"	uvec2 p = gl_GlobalInvocationID.xy;\n"
			"	uint pattern = (p.x ^ p.y) & 255u;\n"
			"	vec4 color = vec4(float(pattern) / 255.0,\n"
			"		float(p.x) / 255.0, float(p.y) / 255.0, 1.0);\n"
			"	imageStore(image, ivec2(p), color);\n"
			"}\n");
		program = glCreateProgram();
		glAttachShader(program, shader);
		glLinkProgram(program);
	}
	glUseProgram(program);
	glBindImageTexture(0, colorTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
		GL_RGBA8);
	glDispatchCompute(kSize / 8, kSize / 8, 1);
	glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT
		| GL_TEXTURE_UPDATE_BARRIER_BIT);
}


struct Scene {
	const char *name;
	void (*draw)(void);
};

static const struct Scene kScenes[] = {
	{"gradient", SceneGradient},
	{"texture", SceneTexture},
	{"depth", SceneDepth},
	{"blend", SceneBlend},
	{"msaa", SceneMsaa},
	{"instancing", SceneInstancing},
	{"compute", NULL},
};


static int
Selected(const char *name, int argc, char **argv)
{
	if (argc <= 2)
		return 1;
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], name) == 0)
			return 1;
	}
	return 0;
}


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	const char *directory = argc > 1 ? argv[1] : ".";
	const int width = 64, height = 64;

	// --- Wayland window
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		printf("[!] wl_display_connect failed\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &sRegistryListener, NULL);
	wl_display_roundtrip(display);
	if (sCompositor == NULL || sWmBase == NULL) {
		printf("[!] no wl_compositor or xdg_wm_base\n");
		return 1;
	}
	struct wl_surface *surface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface *xdgSurface = xdg_wm_base_get_xdg_surface(sWmBase,
		surface);
	xdg_surface_add_listener(xdgSurface, &sSurfaceListener, NULL);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdgSurface);
	xdg_toplevel_add_listener(toplevel, &sToplevelListener, NULL);
	xdg_toplevel_set_title(toplevel, "glref");
	wl_surface_commit(surface);
	while (!sConfigured)
		wl_display_dispatch(display);
	printf("window configured\n");

	// --- EGL
	EGLDisplay eglDisplay = eglGetDisplay((EGLNativeDisplayType)display);
	EGLint major, minor;
	if (!eglInitialize(eglDisplay, &major, &minor)) {
		printf("[!] eglInitialize: %#x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, %s\n", major, minor,
		eglQueryString(eglDisplay, EGL_VENDOR));
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint configAttributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_NONE
	};
	EGLConfig config;
	EGLint count = 0;
	if (!eglChooseConfig(eglDisplay, configAttributes, &config, 1, &count)
		|| count == 0) {
		printf("[!] eglChooseConfig: %#x\n", eglGetError());
		return 1;
	}
	const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
	EGLContext context = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT,
		contextAttributes);
	if (context == EGL_NO_CONTEXT) {
		printf("[!] eglCreateContext: %#x\n", eglGetError());
		return 1;
	}
	struct wl_egl_window *eglWindow = wl_egl_window_create(surface, width,
		height);
	EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, config,
		(EGLNativeWindowType)eglWindow, NULL);
	if (eglSurface == EGL_NO_SURFACE
		|| !eglMakeCurrent(eglDisplay, eglSurface, eglSurface, context)) {
		printf("[!] eglMakeCurrent: %#x\n", eglGetError());
		return 1;
	}
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	// the scene's framebuffer: RGBA8 texture and 24 bit depth
	GLuint colorTexture, depthBuffer, framebuffer;
	glGenTextures(1, &colorTexture);
	glBindTexture(GL_TEXTURE_2D, colorTexture);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, kSize, kSize);
	glGenRenderbuffers(1, &depthBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSize,
		kSize);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, colorTexture, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, depthBuffer);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("[!] framebuffer incomplete\n");
		return 1;
	}

	static uint8_t pixels[kSize * kSize * 4], flipped[kSize * kSize * 4];
	int failed = 0;
	for (size_t i = 0; i < sizeof(kScenes) / sizeof(kScenes[0]); i++) {
		const struct Scene *scene = &kScenes[i];
		if (!Selected(scene->name, argc, argv))
			continue;
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glViewport(0, 0, kSize, kSize);
		glClearColor(0.1f, 0.1f, 0.2f, 1);
		glClearDepthf(1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (scene->draw != NULL)
			scene->draw();
		else
			SceneCompute(colorTexture);
		glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		GLenum error = glGetError();
		// GL rows are bottom up
		for (int y = 0; y < kSize; y++) {
			memcpy(flipped + y * kSize * 4,
				pixels + (kSize - 1 - y) * kSize * 4, kSize * 4);
		}
		char path[1024];
		snprintf(path, sizeof(path), "%s/gl-%s.png", directory, scene->name);
		if (error != GL_NO_ERROR || WritePng(path, flipped, kSize, kSize) != 0) {
			printf("[!] %s: GL error %#x or can't write %s\n", scene->name,
				error, path);
			failed = 1;
			continue;
		}
		printf("%s: wrote %s\n", scene->name, path);
	}

	glDeleteFramebuffers(1, &framebuffer);
	glDeleteRenderbuffers(1, &depthBuffer);
	glDeleteTextures(1, &colorTexture);
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);
	eglDestroySurface(eglDisplay, eglSurface);
	wl_egl_window_destroy(eglWindow);
	eglDestroyContext(eglDisplay, context);
	eglTerminate(eglDisplay);
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdgSurface);
	wl_surface_destroy(surface);
	// see glwl.c
	wl_display_roundtrip(display);
	usleep(200000);
	wl_display_disconnect(display);
	printf("%s\n", failed ? "[!] failed" : "all scenes written");
	return failed;
}
