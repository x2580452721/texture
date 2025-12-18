#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <cmath>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// =================== 工具：GL 检查（调试用，可留着） ===================
static void GLCheck(const char* tag)
{
#ifdef _DEBUG
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "[GL ERROR] " << tag << " : 0x" << std::hex << err << std::dec << "\n";
    }
#endif
}

// =================== Shader 源码：Scene Pass（法线贴图 + 动画 + alpha混合） ===================
static const char* sceneVS = R"(
#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec2 aUV;
layout (location=2) in vec3 aNormal;
layout (location=3) in vec3 aTangent;
layout (location=4) in vec3 aBitangent;

out VS_OUT {
    vec2 uv;
    vec3 fragPos;
    mat3 TBN;
} vout;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    vout.fragPos = worldPos.xyz;
    vout.uv = aUV;

    vec3 N = normalize(mat3(model) * aNormal);
    vec3 T = normalize(mat3(model) * aTangent);
    vec3 B = normalize(mat3(model) * aBitangent);
    vout.TBN = mat3(T, B, N);

    gl_Position = proj * view * worldPos;
}
)";

static const char* sceneFS = R"(
#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec2 uv;
    vec3 fragPos;
    mat3 TBN;
} fin;

uniform sampler2D uDiffuse;
uniform sampler2D uOverlay;   // RGBA
uniform sampler2D uNormal;    // 法线贴图

uniform float uTime;
uniform float uMix;           // 0~1，叠加强度
uniform vec3  uLightPos;
uniform vec3  uViewPos;

uniform int   uUseNormalMap;  // 🔥 新增：0/1 开关

vec2 animatedUV(vec2 uv, float t)
{
    uv += vec2(t * 0.10, 0.0);

    float amp = 0.02;
    float freq = 10.0;
    uv.y += sin(uv.x * freq + t * 2.0) * amp;

    return uv;
}

void main()
{
    vec2 uv = animatedUV(fin.uv, uTime);

    vec3 base = texture(uDiffuse, uv).rgb;

    vec4 over = texture(uOverlay, uv);
    vec3 color = base;
    color += over.rgb * over.a * uMix;

    // ===== 法线贴图（可开关）=====
    // 默认用几何法线：TBN 的第三列就是 N（面朝 +Z 时约等于 (0,0,1)）
    vec3 N = normalize(fin.TBN[2]);

    if (uUseNormalMap == 1)
    {
        vec3 n = texture(uNormal, uv).rgb;
        n = n * 2.0 - 1.0;

        // 如果你发现凹凸反了，把下面这行取消注释（DX/OpenGL 绿通道方向差异）
        // n.g = -n.g;

        N = normalize(fin.TBN * normalize(n));
    }

    vec3 L = normalize(uLightPos - fin.fragPos);
    vec3 V = normalize(uViewPos - fin.fragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 48.0);

    vec3 ambient = 0.15 * color;
    vec3 diffuse = diff * color;
    vec3 specular = vec3(0.25) * spec;

    vec3 finalColor = ambient + diffuse + specular;
    FragColor = vec4(finalColor, 1.0);
}
)";

// =================== Shader：Screen Pass（FBO 后处理） ===================
static const char* screenVS = R"(
#version 330 core
layout (location=0) in vec2 aPos;
layout (location=1) in vec2 aUV;
out vec2 vUV;
void main()
{
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* screenFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uScene;
uniform float uTime;
uniform int uMode; // 0=正常 1=灰度 2=边缘 3=轻微模糊

vec3 sampleScene(vec2 uv) {
    return texture(uScene, uv).rgb;
}

void main()
{
    vec2 uv = vUV;
    float scan = 0.98 + 0.02 * sin(uv.y * 900.0 + uTime * 8.0);

    vec2 texel = 1.0 / vec2(textureSize(uScene, 0));

    vec3 col = sampleScene(uv);

    if (uMode == 1) {
        float g = dot(col, vec3(0.299, 0.587, 0.114));
        col = vec3(g);
    }
    else if (uMode == 2) {
        float tl = dot(sampleScene(uv + texel * vec2(-1,  1)), vec3(0.299,0.587,0.114));
        float tc = dot(sampleScene(uv + texel * vec2( 0,  1)), vec3(0.299,0.587,0.114));
        float tr = dot(sampleScene(uv + texel * vec2( 1,  1)), vec3(0.299,0.587,0.114));
        float ml = dot(sampleScene(uv + texel * vec2(-1,  0)), vec3(0.299,0.587,0.114));
        float mr = dot(sampleScene(uv + texel * vec2( 1,  0)), vec3(0.299,0.587,0.114));
        float bl = dot(sampleScene(uv + texel * vec2(-1, -1)), vec3(0.299,0.587,0.114));
        float bc = dot(sampleScene(uv + texel * vec2( 0, -1)), vec3(0.299,0.587,0.114));
        float br = dot(sampleScene(uv + texel * vec2( 1, -1)), vec3(0.299,0.587,0.114));

        float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
        float gy = -bl - 2.0*bc - br + tl + 2.0*tc + tr;
        float e = clamp(sqrt(gx*gx + gy*gy), 0.0, 1.0);
        col = vec3(e);
    }
    else if (uMode == 3) {
        vec3 sum = vec3(0.0);
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                sum += sampleScene(uv + texel * vec2(x, y));
            }
        }
        col = sum / 9.0;
    }

    col *= scan;
    col = pow(col, vec3(1.0/2.2));
    FragColor = vec4(col, 1.0);
}
)";

// =================== Shader 编译/链接（带检查） ===================
static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
        std::exit(-1);
    }
    return s;
}

static GLuint createProgram(const char* vs, const char* fs)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, 2048, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
        std::exit(-1);
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// =================== 纹理加载（带 mipmap + 对齐修复） ===================
static GLuint loadTexture2D(const char* path)
{
    stbi_set_flip_vertically_on_load(true);

    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (!data) {
        std::cerr << "❌ Failed to load texture: " << path << "\n";
        std::exit(-1);
    }

    GLenum format = (ch == 4) ? GL_RGBA : (ch == 3 ? GL_RGB : GL_RED);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
    return tex;
}

// 🔥 新增：切换 mipmap（通过 MIN_FILTER 选择是否使用 mipmap）
static void setMipmap(GLuint tex, bool enable)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    if (enable) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
}

// =================== 生成一个带 TBN 的四边形（法线贴图必需） ===================
struct Vertex {
    float px, py, pz;
    float u, v;
    float nx, ny, nz;
    float tx, ty, tz;
    float bx, by, bz;
};

static void buildQuad(std::vector<Vertex>& outV, std::vector<unsigned int>& outI)
{
    Vertex v0{ -0.8f,-0.6f,0,  0,0,   0,0,1,   1,0,0,   0,1,0 };
    Vertex v1{ 0.8f,-0.6f,0,  1,0,   0,0,1,   1,0,0,   0,1,0 };
    Vertex v2{ 0.8f, 0.6f,0,  1,1,   0,0,1,   1,0,0,   0,1,0 };
    Vertex v3{ -0.8f, 0.6f,0,  0,1,   0,0,1,   1,0,0,   0,1,0 };

    outV = { v0,v1,v2,v3 };
    outI = { 0,1,2, 0,2,3 };
}

// =================== 主函数 ===================
int main()
{
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1000, 700, "Texture Demo", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ------------------- 几何：quad -------------------
    std::vector<Vertex> V;
    std::vector<unsigned int> I;
    buildQuad(V, I);

    GLuint vao = 0, vbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(V.size() * sizeof(Vertex)), V.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(I.size() * sizeof(unsigned int)), I.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(11 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    // ------------------- Shader Programs -------------------
    GLuint sceneProg = createProgram(sceneVS, sceneFS);
    GLuint screenProg = createProgram(screenVS, screenFS);

    // ------------------- 纹理 -------------------
    GLuint texDiffuse = loadTexture2D("assets/diffuse.jpg");
    GLuint texOverlay = loadTexture2D("assets/overlay.png");
    GLuint texNormal = loadTexture2D("assets/normal.png");

    // ------------------- FBO（后处理） -------------------
    int fbW = 1000, fbH = 700;
    GLuint fbo = 0, colorTex = 0, rbo = 0;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbW, fbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbW, fbH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FBO not complete!\n";
        return -1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ------------------- Screen Quad（全屏） -------------------
    float screenVerts[] = {
        -1,-1,  0,0,
         1,-1,  1,0,
         1, 1,  1,1,
        -1, 1,  0,1
    };
    unsigned int screenIdx[] = { 0,1,2, 0,2,3 };

    GLuint svao = 0, svbo = 0, sebo = 0;
    glGenVertexArrays(1, &svao);
    glGenBuffers(1, &svbo);
    glGenBuffers(1, &sebo);

    glBindVertexArray(svao);
    glBindBuffer(GL_ARRAY_BUFFER, svbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screenVerts), screenVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(screenIdx), screenIdx, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // ------------------- Uniform location 缓存 -------------------
    glUseProgram(sceneProg);
    GLint locModel = glGetUniformLocation(sceneProg, "model");
    GLint locView = glGetUniformLocation(sceneProg, "view");
    GLint locProj = glGetUniformLocation(sceneProg, "proj");
    GLint locTime = glGetUniformLocation(sceneProg, "uTime");
    GLint locMix = glGetUniformLocation(sceneProg, "uMix");
    GLint locLP = glGetUniformLocation(sceneProg, "uLightPos");
    GLint locVP = glGetUniformLocation(sceneProg, "uViewPos");
    GLint locD = glGetUniformLocation(sceneProg, "uDiffuse");
    GLint locO = glGetUniformLocation(sceneProg, "uOverlay");
    GLint locN = glGetUniformLocation(sceneProg, "uNormal");
    GLint locUseNM = glGetUniformLocation(sceneProg, "uUseNormalMap"); // 🔥

    if (locD != -1) glUniform1i(locD, 0);
    if (locO != -1) glUniform1i(locO, 1);
    if (locN != -1) glUniform1i(locN, 2);

    glUseProgram(screenProg);
    GLint locScene = glGetUniformLocation(screenProg, "uScene");
    GLint locSTime = glGetUniformLocation(screenProg, "uTime");
    GLint locMode = glGetUniformLocation(screenProg, "uMode");
    if (locScene != -1) glUniform1i(locScene, 0);

    // ------------------- 简单矩阵（不引 glm） -------------------
    auto mat4Identity = []() {
        float m[16] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
        return std::vector<float>(m, m + 16);
        };

    std::vector<float> M = mat4Identity();
    std::vector<float> Vw = mat4Identity();
    std::vector<float> P = mat4Identity();

    float fov = 60.0f * 3.1415926f / 180.0f;
    float aspect = (float)fbW / (float)fbH;
    float zn = 0.1f, zf = 10.0f;
    float tt = tanf(fov / 2.0f) * zn;
    float rr = tt * aspect;
    float persp[16] = {
        zn / rr, 0,   0,                0,
        0,   zn / tt, 0,                0,
        0,   0,  -(zf + zn) / (zf - zn),     -1,
        0,   0,  -(2 * zf * zn) / (zf - zn), 0
    };
    P.assign(persp, persp + 16);

    Vw = mat4Identity();
    Vw[14] = -2.0f;

    // ------------------- 交互状态 -------------------
    int postMode = 0;                 // 1~4：后处理
    bool keyLock = false;             // 1~4 锁

    bool useNormalMap = true;         // N 键开关
    bool nLock = false;

    bool useMipmap = true;            // M 键开关
    bool mLock = false;

    // 首次把 mipmap 状态应用一次
    setMipmap(texDiffuse, useMipmap);
    setMipmap(texOverlay, useMipmap);
    setMipmap(texNormal, useMipmap);

    auto updateTitle = [&](void) {
        std::string title = "Texture Demo | N(NormalMap): ";
        title += (useNormalMap ? "ON" : "OFF");
        title += " | M(Mipmap): ";
        title += (useMipmap ? "ON" : "OFF");
        title += " | Post(1-4): ";
        title += std::to_string(postMode + 1);
        glfwSetWindowTitle(window, title.c_str());
        };
    updateTitle();

    // ------------------- 主循环 -------------------
    while (!glfwWindowShouldClose(window))
    {
        float time = (float)glfwGetTime();

        // 1~4 切换后处理模式
        if (!keyLock) {
            if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) { postMode = 0; keyLock = true; updateTitle(); }
            if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) { postMode = 1; keyLock = true; updateTitle(); }
            if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) { postMode = 2; keyLock = true; updateTitle(); }
            if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) { postMode = 3; keyLock = true; updateTitle(); }
        }
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE &&
            glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE &&
            glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE &&
            glfwGetKey(window, GLFW_KEY_4) == GLFW_RELEASE) {
            keyLock = false;
        }

        // N：开关法线贴图
        if (!nLock && glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
            useNormalMap = !useNormalMap;
            nLock = true;
            updateTitle();
        }
        if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) nLock = false;

        // M：开关 mipmap
        if (!mLock && glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
            useMipmap = !useMipmap;
            mLock = true;

            setMipmap(texDiffuse, useMipmap);
            setMipmap(texOverlay, useMipmap);
            setMipmap(texNormal, useMipmap);

            updateTitle();
        }
        if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) mLock = false;

        // =================== Pass 1：渲染到 FBO ===================
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, fbW, fbH);
        glEnable(GL_DEPTH_TEST);

        glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(sceneProg);

        if (locModel != -1) glUniformMatrix4fv(locModel, 1, GL_FALSE, M.data());
        if (locView != -1) glUniformMatrix4fv(locView, 1, GL_FALSE, Vw.data());
        if (locProj != -1) glUniformMatrix4fv(locProj, 1, GL_FALSE, P.data());
        if (locTime != -1) glUniform1f(locTime, time);
        if (locMix != -1) glUniform1f(locMix, 0.85f);

        if (locUseNM != -1) glUniform1i(locUseNM, useNormalMap ? 1 : 0);

        float lightX = 1.2f * cosf(time * 0.7f);
        float lightY = 0.8f;
        float lightZ = 1.5f + 0.5f * sinf(time * 0.7f);
        if (locLP != -1) glUniform3f(locLP, lightX, lightY, lightZ);
        if (locVP != -1) glUniform3f(locVP, 0.0f, 0.0f, 2.0f);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texDiffuse);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texOverlay);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texNormal);

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // =================== Pass 2：屏幕四边形后处理 ===================
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, fbW, fbH);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(screenProg);
        if (locSTime != -1) glUniform1f(locSTime, time);
        if (locMode != -1) glUniform1i(locMode, postMode);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTex);

        glBindVertexArray(svao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        GLCheck("frame");
    }

    glfwTerminate();
    return 0;
}
