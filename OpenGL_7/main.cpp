﻿#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include "common.h"
#include "math/math3d.h"
#include "math/mathgl.h"
#include "OpenGL.h"
#include "GLWindow.h"
#include "Shader.h"
#include "Texture.h"
#include "Mesh.h"
#include "Camera.h"
#include "Light.h"
#include "Material.h"

// вспомогательный макрос
#define LOAD_SHADER(name) \
	ShaderProgramCreateFromFile("data/shaders/" name ".vert", "data/shaders/" name ".frag")

// структура описания пост-эффекта
struct Posteffect
{
	uint8_t    key;
	const char *shader;
	GLuint     program;
};

// структура с описанием вершины для полноэкранного прямоугольника
struct fsqVertex
{
	float3 position;
	float2 texcoord;
};

// индекс шейдерной программы
static GLuint depthProgram = 0, shadowmapProgram = 0, posteffectProgram = 0;

// индекс FBO
static GLuint depthFBO = 0;
static const uint32_t posteffectFBOsCount = 3;
static GLuint posteffectFBOs[posteffectFBOsCount];

// индексы текстур
static GLuint depthTexture = 0, colorTexture = 0;
static GLuint posteffectTextures[posteffectFBOsCount];
static GLuint posteffectDepthTextures[posteffectFBOsCount];

// граница фильтрованного изображения
float posteffectBorder = 0.5;

// параметры для фильтраций
float posteffectGamma = 0.5;
float posteffectEdgeThreshold = 0.2;

// VAO и VBO для полноэкранного прямоугольника
static GLuint fsqVAO = 0, fsqVBO = 0;

// положение курсора и его смещение с последнего кадра
static int cursorPos[2] = {0,0}, rotateDelta[2] = {0,0}, moveDelta[2] = {0,0};

static const uint32_t meshCount = 3;
static Mesh           meshes[meshCount];
static Material       materials[meshCount];
static GLuint materialTextures[meshCount];

static float3 torusRotation = {0.0f, 0.0f, 0.0f};

static Light  directionalLight;
static Camera mainCamera, lightCamera;

// пост-эффекты 
static const uint32_t posteffectsCount = 7 + 3;
static Posteffect posteffects[posteffectsCount] = {
	// изначальные постэффекты
	{VK_F1, "data/shaders/default_filters/normal.frag",     0},
	{VK_F2, "data/shaders/default_filters/grayscale.frag",  0},
	{VK_F3, "data/shaders/default_filters/sepia.frag",      0},
	{VK_F4, "data/shaders/default_filters/inverse.frag",    0},
	{VK_F5, "data/shaders/default_filters/blur.frag",       0},
	{VK_F6, "data/shaders/default_filters/emboss.frag",     0},
	{VK_F7, "data/shaders/default_filters/aberration.frag", 0},
	// добавленные постэффекты
	{VK_F8, "data/shaders/extra_filters/gamma.frag", 0},
	{VK_F9, "data/shaders/extra_filters/edge.frag", 0},
	{VK_F10, "data/shaders/extra_filters/log.frag", 0}
};
uint32_t posteffectChoice = 0;

// фильтры

// сглаживающие фильтры
static const uint32_t blurFiltersCount = 3;
static mat3 blurFilters[blurFiltersCount] = {
	// усредняющий фильтр
	mat3(1.0, 1.0, 1.0,
		 1.0, 1.0, 1.0,
		 1.0, 1.0, 1.0) / 9, 
	// стандартный blur фильтр
	mat3(1.0, 2.0, 1.0,
		 2.0, 4.0, 2.0,
		 1.0, 2.0, 1.0) / 16,
	mat3(0.0, -1.0, 0.0,
		 -1.0, 5.0, -1.0,
		 0.0, -1.0, 0.0)
};
uint32_t blurFilterChoice = 0;

// контрсатоповышающие фильтры

// разностные фильтры
static const uint32_t edgeFiltersCount = 5;
static mat3 edgeFilters[edgeFiltersCount] = {
	mat3(2.0, 0.0, 0.0,
		 0.0, -1.0, 0.0,
	     0.0, 0.0, -1.0) / 2,
	// оператор Прюитта
	mat3(-1.0, 0.0, 1.0,
		 -1.0, 0.0, 1.0,
		 -1.0, 0.0, 1.0) / 3,
	// оператор Собеля
	mat3(-1.0, -2.0, -1.0,
		  0.0, 0.0, 0.0,
		  1.0, 2.0, 1.0) / 4,
	// оператор Шарра
	mat3(-3.0, 0.0, 3.0,
		 -10.0, 0.0, 10.0,
		 -3.0, 0.0, 3.0) / 16,
	// оператор Лапласа
	mat3(-1.0, -1.0,-1.0,
		 -1.0,  8.0, -1.0,
		 -1.0, -1.0, -1.0)
};
uint32_t edgeFilterChoice = 0;
uint32_t embossFilterChoice = 0;

// вершины полноэкранного прямоугольника
// координаты и текстурные координаты (текстура цвета)
const fsqVertex fsqVertices[6] = {
	{{-1.0f, -1.0f, 0.0f}, {0.0f,0.0f}},
	{{ 1.0f, -1.0f, 0.0f}, {1.0f,0.0f}},
	{{-1.0f,  1.0f, 0.0f}, {0.0f,1.0f}},
	{{ 1.0f, -1.0f, 0.0f}, {1.0f,0.0f}},
	{{ 1.0f,  1.0f, 0.0f}, {1.0f,1.0f}},
	{{-1.0f,  1.0f, 0.0f}, {0.0f,1.0f}}
};

// инициализаця OpenGL
bool GLWindowInit(const GLWindow &window)
{
	// спрячем курсор
	InputShowCursor(false);

	// устанавливаем вьюпорт на все окно
	glViewport(0, 0, window.width, window.height);

	// параметры OpenGL
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	// создадим и загрузим шейдерные программы, в т.ч. создания текстуры теней
	if ((depthProgram = LOAD_SHADER("depth")) == 0
		|| (shadowmapProgram = LOAD_SHADER("shadowmap")) == 0)
	{
		return false;
	}

	for (uint32_t p = 0; p < posteffectsCount; ++p)
	{
		posteffects[p].program = ShaderProgramCreateFromFile("data/shaders/posteffect.vert", posteffects[p].shader);

		if (posteffects[p].program == 0)
			return false;
	}

	posteffectProgram = posteffects[0].program;

	// настроим направленный источник освещения
	LightDefault(directionalLight, LT_DIRECTIONAL);
	directionalLight.position.set(10.0f, 10.0f, 10.0f, 0.0f);

	// загрузим текстуры
	materialTextures[0] = TextureCreateFromTGA("data/textures/grass.tga");
	materialTextures[1] = TextureCreateFromTGA("data/textures/floor.tga");
	materialTextures[2]	= TextureCreateFromTGA("data/textures/texture.tga");					

	// создадим текстуру для хранения глубины
	depthTexture = TextureCreateDepth(window.width * 2, window.height * 2);

	// создадим "пустые" текстуры для FBO размером с текущее окно
	for (int i = 0; i < posteffectFBOsCount; i++)
	{
		posteffectTextures[i] = TextureCreateEmpty(GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, window.width, window.height);
		posteffectDepthTextures[i] = TextureCreateEmpty(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT,
			GL_UNSIGNED_BYTE, window.width, window.height);
	}

	// создадим примитивы и настроим материалы
	// плоскость под вращающимся тором
	MeshCreateQuad(meshes[0], vec3(0.0f, -1.6f, 0.0f), 30.0f);
	MaterialDefault(materials[0]);
	materials[0].texture = materialTextures[0];
	materials[0].diffuse.set(0.7f, 1.0f, 0.7f, 1.0f);

	// вращающийся тор
	MeshCreateTorus(meshes[1], vec3(0.0f, 1.2f, 0.0f), 2.0f);
	MaterialDefault(materials[1]);
	materials[1].texture = materialTextures[1];
	materials[1].diffuse.set(0.3f, 0.5f, 1.0f, 1.0f);
	materials[1].specular.set(0.8f, 0.8f, 0.8f, 1.0f);
	materials[1].shininess = 20.0f;

	// вращающийся тор
	MeshCreateTorus(meshes[2], vec3(0.0f, 1.2f, 0.0f), 1.0f);
	MaterialDefault(materials[2]);
	materials[2].texture = materialTextures[2];
	materials[2].diffuse.set(1.0f, 0.5f, 0.3f, 1.0f);
	materials[2].specular.set(0.8f, 0.8f, 0.8f, 1.0f);
	materials[2].shininess = 20.0f;

	// создадим и настроим камеру
	const float aspectRatio = (float)window.width / (float)window.height;
	CameraLookAt(mainCamera, vec3(-5.0f, 10.0f, 10.0f), vec3_zero, vec3_y);
	CameraPerspective(mainCamera, 45.0f, aspectRatio, 0.5f, 100.0f);

	// камера источника света
	CameraLookAt(lightCamera, directionalLight.position, -directionalLight.position, vec3_y);
	CameraOrtho(lightCamera, -5.0f, 5.0f, -5.0f, 5.0f, -10.0f, 20.0f);

	GLenum fboStatus;

	// создаем FBO для рендера глубины в текстуру
	glGenFramebuffers(1, &depthFBO);
	// делаем созданный FBO текущим
	glBindFramebuffer(GL_FRAMEBUFFER, depthFBO);

	// отключаем вывод цвета в текущий FBO
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	// указываем для текущего FBO текстуру, куда следует производить рендер глубины
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture, 0);

	// проверим текущий FBO на корректность
	if ((fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE)
	{
		LOG_ERROR("glCheckFramebufferStatus error 0x%X\n", fboStatus);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// создаем FBO для множественной фильтрации
	glGenFramebuffers(posteffectFBOsCount, posteffectFBOs);

	for (int i = 0; i < posteffectFBOsCount; i++)
	{
		// делаем созданный FBO текущим
		glBindFramebuffer(GL_FRAMEBUFFER, posteffectFBOs[i]);

		// присоединяем текстуры к FBO
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, posteffectTextures[i], 0);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, posteffectDepthTextures[i], 0);

		// проверим текущий FBO на корректность
		if ((fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE)
		{
			LOG_ERROR("glCheckFramebufferStatus error 0x%X\n", fboStatus);
			return false;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// создадим VAO и VBO для рендера полноэкранного прямоугольника
	glGenVertexArrays(1, &fsqVAO);
	glBindVertexArray(fsqVAO);
	glGenBuffers(1, &fsqVBO);
	// заполняем VBO вершинными данными
	glBindBuffer(GL_ARRAY_BUFFER, fsqVBO);
	glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(fsqVertex), fsqVertices, GL_STATIC_DRAW);
	// задаем параметры расположения вершинных атрибутов в VBO
	glVertexAttribPointer(VERT_POSITION, 3, GL_FLOAT, GL_FALSE,
		sizeof(fsqVertex), GL_OFFSET(0));
	glEnableVertexAttribArray(VERT_POSITION);

	glVertexAttribPointer(VERT_TEXCOORD, 2, GL_FLOAT, GL_FALSE,
		sizeof(fsqVertex), GL_OFFSET(sizeof(float3)));
	glEnableVertexAttribArray(VERT_TEXCOORD);

	// проверим не было ли ошибок
	OPENGL_CHECK_FOR_ERRORS();

	return true;
}

// очистка OpenGL
void GLWindowClear(const GLWindow &window)
{
	(void)window;

	for (uint32_t i = 0; i < meshCount; ++i)
		MeshDestroy(meshes[i]);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &fsqVBO);

	glBindVertexArray(0);
	glDeleteVertexArrays(1, &fsqVAO);

	ShaderProgramDestroy(depthProgram);
	ShaderProgramDestroy(shadowmapProgram);

	for (uint32_t p = 0; p < posteffectsCount; ++p)
		if (posteffects[p].program)
			ShaderProgramDestroy(posteffects[p].program);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &depthFBO);
	glDeleteFramebuffers(2, posteffectFBOs);


	TextureDestroy(depthTexture);
	for (int i = 0; i < meshCount; i++)
	{
		TextureDestroy(materialTextures[i]);
	}
	for (int i = 0; i < posteffectFBOsCount; i++)
	{
		TextureDestroy(posteffectTextures[i]);
		TextureDestroy(posteffectDepthTextures[i]);
	}

	InputShowCursor(true);
}

void RenderScene(GLuint program, const Camera &camera)
{
	// делаем шейдерную программу активной
	ShaderProgramBind(program);

	LightSetup(program, directionalLight);
	CameraSetupLightMatrix(program, lightCamera);

	TextureSetup(program, 1, "depthTexture", depthTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

	for (uint32_t i = 0; i < meshCount; ++i)
	{
		CameraSetup(program, camera, MeshGetModelMatrix(meshes[i]));
		MaterialSetup(program, materials[i]);
		MeshRender(meshes[i]);
	}
}

// функция рендера
void GLWindowRender(const GLWindow &window)
{
	glBindFramebuffer(GL_FRAMEBUFFER, depthFBO);
	glViewport(0, 0, window.width * 2, window.height * 2);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);
	glCullFace(GL_FRONT);

	RenderScene(depthProgram, lightCamera);

	glViewport(0, 0, window.width, window.height);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glCullFace(GL_BACK);

	glBindFramebuffer(GL_FRAMEBUFFER, posteffectFBOs[2]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	RenderScene(shadowmapProgram, mainCamera);

	bool pingpong = true, first_init = true;
	for (int i = 0; i < 10; i++)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, posteffectFBOs[pingpong]);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		// устанавливаем шейдерную программу с реализацией экранного эффекта
		ShaderProgramBind(posteffectProgram);
		// устанавливаем текстуру сцены в 0-й текстурный юнит
		if (first_init)
		{
			TextureSetup(posteffectProgram, 0, "colorTexture", posteffectTextures[2]);
			TextureSetup(posteffectProgram, 2, "depthTexture", posteffectDepthTextures[2]);
		}
		else
		{
			TextureSetup(posteffectProgram, 0, "colorTexture", posteffectTextures[!pingpong]);
			TextureSetup(posteffectProgram, 2, "depthTexture", posteffectDepthTextures[!pingpong]);
		}
		// устанавливаем границу фильтрованного изображения
		ShaderSetFloat(posteffectProgram, "Border", posteffectBorder);
		// устанавливаем параметр для различных фильтраций
		ShaderSetFloat(posteffectProgram, "Gamma", posteffectGamma);
		ShaderSetFloat(posteffectProgram, "EdgeThreshold", posteffectEdgeThreshold);
		switch (posteffectChoice)
		{
		case 4: ShaderSetMatrix(posteffectProgram, "Kernel", blurFilters[blurFilterChoice]); break;
		case 5: ShaderSetMatrix(posteffectProgram, "Kernel", edgeFilters[embossFilterChoice]); break;
		case 8: ShaderSetMatrix(posteffectProgram, "Kernel", edgeFilters[edgeFilterChoice]); break;
		}
		// выводим полноэкранный прямоугольник на экран
		glBindVertexArray(fsqVAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		pingpong = !pingpong;
		first_init = false;
	}

	// устанавливаем дефолтный FBO активным
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// устанавливаем шейдерную программу с реализацией экранного эффекта
	ShaderProgramBind(posteffectProgram);
	// устанавливаем текстуру сцены в 0-й текстурный юнит
	TextureSetup(posteffectProgram, 0, "colorTexture", posteffectTextures[!pingpong]);
	TextureSetup(posteffectProgram, 2, "depthTexture", posteffectDepthTextures[!pingpong]);
	// устанавливаем границу фильтрованного изображения
	ShaderSetFloat(posteffectProgram, "Border", posteffectBorder);
	// устанавливаем параметр для различных фильтраций
	ShaderSetFloat(posteffectProgram, "Gamma", posteffectGamma);
	ShaderSetFloat(posteffectProgram, "EdgeThreshold", posteffectEdgeThreshold);
	switch (posteffectChoice)
	{
	case 4: ShaderSetMatrix(posteffectProgram, "Kernel", blurFilters[blurFilterChoice]); break;
	case 5: ShaderSetMatrix(posteffectProgram, "Kernel", edgeFilters[embossFilterChoice]); break;
	case 8: ShaderSetMatrix(posteffectProgram, "Kernel", edgeFilters[edgeFilterChoice]); break;
	}
	// выводим полноэкранный прямоугольник на экран
	glBindVertexArray(fsqVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	// проверка на ошибки
	OPENGL_CHECK_FOR_ERRORS();
}

// функция обновления
void GLWindowUpdate(const GLWindow &window, double deltaTime)
{
	ASSERT(deltaTime >= 0.0); // проверка на возможность бага

	(void)window;

	// зададим углы поворота тора с учетом времени
	if ((torusRotation[0] += 30.0f * (float)deltaTime) > 360.0f)
		torusRotation[0] -= 360.0f;

	if ((torusRotation[1] += 15.0f * (float)deltaTime) > 360.0f)
		torusRotation[1] -= 360.0f;

	if ((torusRotation[2] += 7.0f * (float)deltaTime) > 360.0f)
		torusRotation[2] -= 360.0f;

	// зададим матрицу вращения торов
	meshes[1].rotation = GLFromEuler(torusRotation[0], torusRotation[1], torusRotation[2]);
	meshes[2].rotation = GLFromEuler(-torusRotation[0], torusRotation[1], -torusRotation[2]);

	// вращаем камеру
	CameraRotate(mainCamera, (float)deltaTime * rotateDelta[1], (float)deltaTime * rotateDelta[0], 0.0f);
	// двигаем камеру
	CameraMove(mainCamera, (float)deltaTime * moveDelta[0], 0.0f, (float)deltaTime * moveDelta[1]);

	rotateDelta[0] = rotateDelta[1] = 0;
	moveDelta[0] = moveDelta[1] = 0;

	OPENGL_CHECK_FOR_ERRORS();
}

// функция обработки ввода с клавиатуры и мыши
void GLWindowInput(const GLWindow &window)
{
	// центр окна
	int32_t xCenter = window.width / 2, yCenter = window.height / 2;

	// выход из приложения по кнопке Esc
	if (InputIsKeyPressed(VK_ESCAPE))
		GLWindowDestroy();

	for (uint32_t p = 0; p < posteffectsCount; ++p)
		if (InputIsKeyPressed(posteffects[p].key))
		{
			posteffectProgram = posteffects[p].program;
			posteffectChoice = p;
		}

	// переключение между оконным и полноэкранным режимом
	// осуществляется по нажатию комбинации Alt+Enter
	if (InputIsKeyDown(VK_MENU) && InputIsKeyPressed(VK_RETURN))
		GLWindowSetSize(window.width, window.height, !window.fullScreen);

	// сдвиг границы фильтрации изображения
	// осуществляется стрелками влево/вправо
	if (posteffectBorder > 0.0 && posteffectChoice != 0)
		posteffectBorder -= 0.01 * (float)InputIsKeyDown(VK_LEFT);
	if (posteffectBorder < 1.0 && posteffectChoice != 0)
		posteffectBorder += 0.01 * (float)InputIsKeyDown(VK_RIGHT);

	// изменение параметра для различных фильтраций
	// осуществляется стрелками вверх/вниз
	float diff= 0.01 * ((float)InputIsKeyDown(VK_UP) - (float)InputIsKeyDown(VK_DOWN));
	if (posteffectGamma + diff > 0.0 && posteffectChoice >= 7)
		posteffectGamma += diff;

	if (posteffectEdgeThreshold > 0.0 && posteffectChoice == 8)
		posteffectEdgeThreshold -= 0.005 * (float)InputIsKeyDown(VK_DOWN);
	if (posteffectEdgeThreshold < 1.0 && posteffectChoice == 8)
		posteffectEdgeThreshold += 0.005 * (float)InputIsKeyDown(VK_UP);
	
	// выбор оператора для пространственных фильтраций
	// осуществляется PageUp/PageDown
	if (InputIsKeyPressed(VK_PRIOR))
	{
		switch (posteffectChoice)
		{
			case 4:
				blurFilterChoice += (int)(blurFilterChoice < blurFiltersCount - 1);
				break;
			case 5:
				embossFilterChoice += (int)(embossFilterChoice < edgeFiltersCount - 1);
				break;
			case 8:
				edgeFilterChoice += (int)(edgeFilterChoice < edgeFiltersCount - 1);
				break;
		}
	}
	if (InputIsKeyPressed(VK_NEXT))
	{
		switch (posteffectChoice)
		{
			case 4:
				blurFilterChoice -= (int)(blurFilterChoice > 0);
				break;
			case 5:
				embossFilterChoice -= (int)(embossFilterChoice > 0);
				break;
			case 8:
				edgeFilterChoice -= (int)(edgeFilterChoice > 0);
				break;
		}
	}

	moveDelta[0] = 10 * ((int)InputIsKeyDown('D') - (int)InputIsKeyDown('A'));
	moveDelta[1] = 10 * ((int)InputIsKeyDown('S') - (int)InputIsKeyDown('W'));

	InputGetCursorPos(cursorPos, cursorPos + 1);
	rotateDelta[0] += cursorPos[0] - xCenter;
	rotateDelta[1] += cursorPos[1] - yCenter;
	InputSetCursorPos(xCenter, yCenter);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	int result;

	LoggerCreate("15_OpenGL_6.log");

	if (!GLWindowCreate("15_OpenGL_6. Image Processing", 800, 600, false))
		return 1;

	result = GLWindowMainLoop();

	GLWindowDestroy();
	LoggerDestroy();

	return result;
}