#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <xaudio2.h>
#include <tchar.h>

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_glfw.h>
#include <ImGui/imgui_impl_opengl3.h>
#include <ImGui/implot.h>
#include <ImGui/implot_internal.h>

#include <shaders.h>
#include <stb_image.h>

#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>

#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'

// GLFW callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Copied DPH functions
bool valueInCircle(float x1, float y1, float x2, float y2, float R);

// Miscellany
void updateRNG(std::uniform_int_distribution<>& xdist, std::uniform_int_distribution<>& ydist, int width, int height, float zoom, float tsy, float tsx);
void restartGame(GLFWwindow* window);
void resetStats(GLFWwindow* window);

// ImGui
namespace ImGui 
{
    bool SliderDouble(const char* label, double* v, double v_min, double v_max, const char* format = NULL, ImGuiSliderFlags flags = 0) 
    {
        return SliderScalar(label, ImGuiDataType_Double, v, &v_min, &v_max, format, flags);
    }
}

// XAudio2 helpers
HRESULT FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition);
HRESULT ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset);
void playSource(IXAudio2SourceVoice* voice, XAUDIO2_BUFFER* buffer);

// Play area constexprs
constexpr glm::vec3 outerQuadScale(800.0f, 800.0f, 1.0f);
constexpr glm::vec3 innerQuadScale(800.0f - (800.0f / 3.0f), 800.0f - (800.0f / 3.0f), 1.0f);
// Score constexprs
constexpr double MAX_EPS_RANGE = 5.0;

// Globals because I can't specify GLFW callback arguments (Although updateRNG and the RNG globals are only a thing because I fucked up the ortho zoom shit)

// Screen globals
unsigned int SCR_WIDTH = 1280;
unsigned int SCR_HEIGHT = 720;

// ImGui globals
bool show_settings_windows = true;
float zoom = 1.00f;
bool axiaCursor = false;

// Target / Cursor globals
glm::vec3 targetSize(60.0f, 60.0f, 1.0f);
glm::vec3 cursorPos(0.0f, 0.0f, 0.0f);
glm::vec3 targetPos(0.0f, 0.0f, 0.0f);

// Logic globals
double startingTime = 0.0;
double startingHp = 100.0;
double hp = startingHp;
std::vector<double> reactionTimes{};
std::vector<double> eatTimes{}; // Need this for ImPlot
enum GameStates { INGAME = 0, PREGAME = 1, POSTGAME = 2 };
GameStates gameState = PREGAME;
double elapsedTime = 0.0;
double averageBalls = 0.0;
double averageReaction = 0.0;
int ballsEaten = 0;
double maxEat = 0.0;
double trueMaxEat = 0.0;
double averageEat = 0.0;
double score = 0.0;
double scorePerBall = 35.0;
double cheeseThreshold = 100.0;

// RNG globals
std::random_device rd;
std::mt19937 gen(rd());
int lb_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.x);
int ub_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.x);
int lb_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.y) + 30.0f;
int ub_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.y) - 30.0f;
std::uniform_int_distribution<> distrib_x(lb_x, ub_x);
std::uniform_int_distribution<> distrib_y(lb_y, ub_y);

int main()
{
    // glfw boilerplate
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "BallSheet++", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    glfwSetWindowMonitor(window, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height, mode->refreshRate);
    SCR_WIDTH = mode->width;
    SCR_HEIGHT = mode->height;
    updateRNG(distrib_x, distrib_y, SCR_WIDTH, SCR_HEIGHT, zoom, targetSize.y, targetSize.x);
    
    targetPos = glm::vec3(SCR_WIDTH / (2 * zoom), SCR_HEIGHT / (2 * zoom), 0.0f);

    glfwSwapInterval(0);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint VBO;
    float vertices[] = 
    {
        // pos       // tex
        0.0f, 1.0f,  0.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        0.0f, 0.0f,  0.0f, 0.0f,
                     
        0.0f, 1.0f,  0.0f, 1.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 0.0f
    };
    GLuint quadVAO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindVertexArray(quadVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    stbi_set_flip_vertically_on_load(true);

    GLuint ballTexture;
    glGenTextures(1, &ballTexture);
    glBindTexture(GL_TEXTURE_2D, ballTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    int width, height, nrChannels;
    unsigned char* data = stbi_load("./res/ball.png", &width, &height, &nrChannels, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    GLuint quadTexture;
    glGenTextures(1, &quadTexture);
    glBindTexture(GL_TEXTURE_2D, quadTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    data = stbi_load("./res/square.png", &width, &height, &nrChannels, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    // audio, all copied msft sample code
    HRESULT hr;
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return hr;

    IXAudio2* pXAudio2 = nullptr;
    if (FAILED(hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
        return hr;

    IXAudio2MasteringVoice* pMasterVoice = nullptr;
    if (FAILED(hr = pXAudio2->CreateMasteringVoice(&pMasterVoice)))
        return hr;

    WAVEFORMATEXTENSIBLE wfx = { 0 };
    XAUDIO2_BUFFER buffer = { 0 };

    const TCHAR* strFileName = _TEXT("./res/idk.wav");
    // Open the file
    HANDLE hFile = CreateFile(
        strFileName,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (INVALID_HANDLE_VALUE == hFile)
        return HRESULT_FROM_WIN32(GetLastError());

    if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD dwChunkSize;
    DWORD dwChunkPosition;
    //check the file type, should be fourccWAVE or 'XWMA'
    FindChunk(hFile, fourccRIFF, dwChunkSize, dwChunkPosition);
    DWORD filetype;
    ReadChunkData(hFile, &filetype, sizeof(DWORD), dwChunkPosition);
    if (filetype != fourccWAVE)
        return S_FALSE;

    FindChunk(hFile, fourccFMT, dwChunkSize, dwChunkPosition);
    ReadChunkData(hFile, &wfx, dwChunkSize, dwChunkPosition);

    //fill out the audio data buffer with the contents of the fourccDATA chunk
    FindChunk(hFile, fourccDATA, dwChunkSize, dwChunkPosition);
    BYTE* pDataBuffer = new BYTE[dwChunkSize];
    ReadChunkData(hFile, pDataBuffer, dwChunkSize, dwChunkPosition);

    buffer.AudioBytes = dwChunkSize;  //size of the audio buffer in bytes
    buffer.pAudioData = pDataBuffer;  //buffer containing audio data
    buffer.Flags = XAUDIO2_END_OF_STREAM; // tell the source voice not to expect any data after this buffer

    IXAudio2SourceVoice* pSourceVoice;
    if (FAILED(hr = pXAudio2->CreateSourceVoice(&pSourceVoice, (WAVEFORMATEX*)&wfx))) return hr;

    if (FAILED(hr = pSourceVoice->SubmitSourceBuffer(&buffer)))
        return hr;

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; 

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    const char* glsl_version = "#version 150";
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Link and compile shader programs
    Shader shader("./res/shader.vert", "./res/shader.frag");  

    // Set up non-global game variables
    bool show_main_settings = true;
    bool show_debug_window = false;
    glm::vec4 clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 target_color(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 cursor_color(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 inner_quad_color(25.0f / 255.0f, 25.0f / 255.0f, 25.0f / 255.0f, 1.0f);
    glm::vec4 outer_quad_color(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f);
    glm::vec3 cursorSize(30.0f, 30.0f, 1.0f);
    double PRESSURE = 60.0;
    double deltaTime = 0.0;
    double lastFrame = 0.0;
    double lastHit = 0.0;

    // Prevent vectors from relocating until Axia/Lorenzo get a 100s score
    reactionTimes.reserve(1024);
    eatTimes.reserve(1024);

    glActiveTexture(GL_TEXTURE0);

    while (!glfwWindowShouldClose(window))
    {
        // Timing
        double currentFrame = glfwGetTime() - startingTime;

        if (gameState == INGAME)
        {
            // HP drain calculation, everything done in the same order as dph on purpose (except no longer calculating Max Eat every frame)
            if (hp < 0)
            {
                restartGame(window);
                continue;
            }
                
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            hp -= (deltaTime) * PRESSURE * std::log(1 + (currentFrame));
        }

        if (gameState != POSTGAME && std::sqrt(std::pow((cursorPos.x - targetPos.x), 2) + std::pow((cursorPos.y - targetPos.y), 2)) < (cursorSize.x + targetSize.x) / 2)
        {
            if (gameState == INGAME)
            {
                reactionTimes.emplace_back(currentFrame - lastHit);
                eatTimes.emplace_back(currentFrame);
                double eat = scorePerBall * ((std::min)(reactionTimes.back(), (cheeseThreshold / 1000.0)) / (cheeseThreshold / 1000.0));
                score += eat;
                hp += eat;
                lastHit = currentFrame;
            }
            else if (gameState == PREGAME)
            {
                gameState = INGAME;
                startingTime = currentFrame;
                lastHit = 0.0;
                lastFrame = 0.0;
                score = 0.0;
                hp = startingHp;
            }

            playSource(pSourceVoice, &buffer);

            float oldx = targetPos.x;
            float oldy = targetPos.y;
            do
            {
                targetPos.x = distrib_x(gen);
                targetPos.y = distrib_y(gen);
            } while (!valueInCircle(targetPos.x, targetPos.y, oldx, oldy, 300) || valueInCircle(targetPos.x, targetPos.y, oldx, oldy, 100));
            // Verbatim what dph did, tldr targets must by >300 px away OR <100px away from previous one
        }

        // Buffers
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // Projections
        shader.use();
        glm::mat4 projection = glm::ortho(0.0f, (float)SCR_WIDTH / zoom, (float)SCR_HEIGHT / zoom, 0.0f, -1.0f, 1.0f);
        shader.setMat4("projection", projection);

        glBindVertexArray(quadVAO);
        glBindTexture(GL_TEXTURE_2D, quadTexture);

        // Outer quad
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(
            ((float)SCR_WIDTH / zoom - 800.0f) / 2.0f,
            ((float)SCR_HEIGHT / zoom - 800.0f) / 2.0f,
            0.0f));
        model = glm::scale(model, outerQuadScale);
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", outer_quad_color * outer_quad_color.w);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Inner quad
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(
            (((float)SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f),
            (((float)SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f),
            0.0f));
        model = glm::scale(model, innerQuadScale);
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", inner_quad_color * inner_quad_color.w);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindTexture(GL_TEXTURE_2D, ballTexture);
        
        // Cursor
        model = glm::mat4(1.0f);
        if (!axiaCursor)
        {
            model = glm::translate(model, glm::vec3(cursorPos.x - (cursorSize.x / 2), cursorPos.y - (cursorSize.y / 2), 0.0f));
            model = glm::scale(model, cursorSize);
        }
        else
        {
            model = glm::translate(model, glm::vec3(cursorPos.x - (30.0f / (2 * zoom)), cursorPos.y - (30.0f / (2 * zoom)), 0.0f));
            model = glm::scale(model, glm::vec3(30.0f / zoom, 30.0f / zoom, 1.0f));
        }  
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", cursor_color * cursor_color.w);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Target
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(targetPos.x - (targetSize.x / 2), targetPos.y - (targetSize.y / 2), 0.0f));
        model = glm::scale(model, targetSize);
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", target_color * target_color.w);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (show_settings_windows)
        {
            // Doubles as a debug window
            //if (show_debug_window)
            //    ImGui::ShowDemoWindow(&show_debug_window);

            // The actual settings + miscellany
            if (show_main_settings)
            {
                float oldZoom = zoom;
                float oldts = targetSize.x;

                ImGui::Begin("Settings");

                ImGui::Text("Press M to toggle this window");
                ImGui::Text("Ctrl+click to type values");

                ImGui::NewLine();

                ImGui::SliderFloat("zoom", &zoom, 0.25f, 2.5f);
                ImGui::ColorEdit4("target color", (float*)&target_color);
                ImGui::ColorEdit4("cursor color", (float*)&cursor_color);
                ImGui::ColorEdit4("inner quad color", (float*)&inner_quad_color);
                ImGui::ColorEdit4("outer quad color", (float*)&outer_quad_color);
                ImGui::ColorEdit4("background color", (float*)&clear_color);

                if (ImGui::Button("OG colors    "))
                {
                    target_color = glm::vec4(250.0f / 255.0f, 150.0f / 255.0f, 200.0f / 255.0f, 1.0f);
                    cursor_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    inner_quad_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    outer_quad_color = glm::vec4(7.0f / 255.0f, 7.0f / 255.0f, 7.0f / 255.0f, 1.0f);
                    clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                }

                if (ImGui::Button("Spiffy colors"))
                {
                    target_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    cursor_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    inner_quad_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    outer_quad_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    clear_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                }

                if (ImGui::Button("Axia colors  "))
                {
                    target_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    cursor_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    inner_quad_color = glm::vec4(25.0f / 255.0f, 25.0f / 255.0f, 25.0f / 255.0f, 1.0f);
                    outer_quad_color = glm::vec4(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f);
                    clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                }

                ImGui::Checkbox("\"O\" cursor", &axiaCursor);

                ImGui::NewLine();

                ImGui::SliderFloat("cursor size", &cursorSize.x, 5.0f, 100.0f);
                ImGui::SliderFloat("target size", &targetSize.x, 5.0f, 100.0f);
                ImGui::SliderDouble("score per ball", &scorePerBall, 0.0, 1000.0);
                ImGui::SliderDouble("pressure", &PRESSURE, 0.0, 1000.0);
                ImGui::SliderDouble("starting hp", &startingHp, 1.0, 1000.0);
                ImGui::SliderDouble("cheese threshold", &cheeseThreshold, 1.0, 1000.0);

                if (ImGui::Button("SB Preset "))
                {
                    cursorSize = glm::vec3(30.0f, 30.0f, 1.0f);
                    targetSize = glm::vec3(30.0f, 30.0f, 1.0f);
                    scorePerBall = 35.0;
                    PRESSURE = 60.0;
                    startingHp = 100.0;
                    cheeseThreshold = 100.0;
                }
                ImGui::SameLine();
                ImGui::Text("dphdmn/ballsheet");

                if (ImGui::Button("BB Preset "))
                {
                    cursorSize = glm::vec3(30.0f, 30.0f, 1.0f);
                    targetSize = glm::vec3(60.0f, 30.0f, 1.0f);
                    scorePerBall = 35.0;
                    PRESSURE = 60.0;
                    startingHp = 100.0;
                    cheeseThreshold = 100.0;
                }
                ImGui::SameLine();
                ImGui::Text("helloimxtal/ballsheet");

                if (ImGui::Button("BBB Preset"))
                {
                    cursorSize = glm::vec3(30.0f, 30.0f, 1.0f);
                    targetSize = glm::vec3(60.0f, 30.0f, 1.0f);
                    scorePerBall = 15.0;
                    PRESSURE = 60.0;
                    startingHp = 49.0;
                    cheeseThreshold = 100.0;
                }
                ImGui::SameLine();
                ImGui::Text("kaoriisbestgirl/burstbigball");

                if (ImGui::Button("SBB Preset"))
                {
                    cursorSize = glm::vec3(30.0f, 30.0f, 1.0f);
                    targetSize = glm::vec3(60.0f, 30.0f, 1.0f);
                    scorePerBall = 26.28;
                    PRESSURE = 60.0;
                    startingHp = 75.0;
                    cheeseThreshold = 100.0;
                }
                ImGui::SameLine();
                ImGui::Text("kaoriisbestgirl/shorterbigball");

                if (ImGui::Button("BC Preset "))
                {
                    cursorSize = glm::vec3(69.0f, 69.0f, 1.0f);
                    targetSize = glm::vec3(69.0f, 69.0f, 1.0f);
                    scorePerBall = 69.0;
                    PRESSURE = 169.0;
                    startingHp = 100.0;
                    cheeseThreshold = 69.0;
                }
                ImGui::SameLine();
                ImGui::Text("dphdmn/ballcheese");

                if (ImGui::Button("SSB Preset"))
                {
                    cursorSize = glm::vec3(5.0f, 5.0f, 1.0f);
                    targetSize = glm::vec3(5.0f, 5.0f, 1.0f);
                    scorePerBall = 40.0;
                    PRESSURE = 40.0;
                    startingHp = 100.0;
                    cheeseThreshold = 2.0;
                }
                ImGui::SameLine();
                ImGui::Text("dphdmn/smallballs");

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::End();

                cursorSize.y = cursorSize.x;
                targetSize.y = targetSize.x;
                if (oldZoom != zoom || oldts != targetSize.x)
                {
                    updateRNG(distrib_x, distrib_y, SCR_WIDTH, SCR_HEIGHT, zoom, targetSize.y, targetSize.x);
                    targetPos.x = ((targetPos.x - (((SCR_WIDTH / oldZoom - 800.0) / 2.0) + 800.0 / 6.0))) + (((SCR_WIDTH / zoom - 800.0) / 2.0) + 800.0 / 6.0);
                    targetPos.y = ((targetPos.y - (((SCR_HEIGHT / oldZoom - 800.0) / 2.0) + 800.0 / 6.0))) + (((SCR_HEIGHT / zoom - 800.0) / 2.0) + 800.0 / 6.0);
                }
            }
        }

        if (gameState == POSTGAME)
        {
            ImGui::SetNextWindowPos(ImVec2(((float)SCR_WIDTH - 800.0f) / 2.0f, ((float)SCR_HEIGHT - 800.0f) / 2.0f));
            ImGui::SetNextWindowSize(ImVec2(outerQuadScale.x, outerQuadScale.y));
            ImGui::SetNextWindowFocus();
            ImPlot::SetNextAxesToFit();
            ImGui::Begin("Results");
            if (ImPlot::BeginPlot("Results"))
            {
                ImPlot::SetupAxes("Elapsed Time", "Reaction Time");
                ImPlot::PlotLine("Graph", &eatTimes.at(0), &reactionTimes.at(0), reactionTimes.size());
                ImPlot::EndPlot();
                ImGui::Text("Time:         %f", elapsedTime);
                ImGui::Text("Avg. Balls/s: %f", averageBalls);
                ImGui::Text("Avg. React:   %f", averageReaction);
                ImGui::SameLine();
                ImGui::Text("(Untrimmed)");
                ImGui::Text("Balls:        %d", ballsEaten);
                ImGui::Text("Score:        %f", score);
                if (maxEat > 0.0)
                {
                    ImGui::Text("Max Eat:      %f", maxEat);
                    ImGui::SameLine();
                    ImGui::Text("(Fastest 5s segment: %f ms cheese corrected avg., %f ms true avg.)", 1000.0 / (maxEat / scorePerBall), trueMaxEat);
                }
                else
                {
                    ImGui::Text("Max Eat:      Last ball hit was <5s, Max Eat measures fastest 5s segment", maxEat);
                }
                ImGui::Text("Avg. Eat:     %f", averageEat);
                ImGui::NewLine();
                ImGui::Text("Press R to close this window");
            }
            ImGui::End();
        }

        ImGui::Render();
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Present and poll
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Free all resources
    glfwTerminate();
    return 0;
}

void restartGame(GLFWwindow* window)
{
    if (reactionTimes.size() == 0)
    {
        resetStats(window);
    }
    if (gameState != INGAME)
    {
        if (gameState == POSTGAME)
        {
            resetStats(window);
        }
        return;
    }

    elapsedTime = glfwGetTime() - startingTime;
    ballsEaten = reactionTimes.size();
    averageEat = score / elapsedTime;

    // Average reaction
    double sum = 0.0;
    for (auto& d : reactionTimes)
    {
        d *= 1000.0;
        sum += d;
    }

    // Max eat
    for (int i = 0; eatTimes.at(i) <= eatTimes.back() - MAX_EPS_RANGE; i++) // a
    {
        double temp = 0;
        double unweightedtemp = 0;
        for (int j = i; eatTimes.at(j) <= eatTimes.at(i) + MAX_EPS_RANGE; j++)
        {
            temp += scorePerBall * ((std::min)(reactionTimes.at(j), cheeseThreshold) / cheeseThreshold);
            unweightedtemp += scorePerBall;
        }
        double candidateMaxEat = temp / MAX_EPS_RANGE;
        if (candidateMaxEat > maxEat)
        {
            maxEat = candidateMaxEat;
            trueMaxEat = 1000.0 / ((unweightedtemp / MAX_EPS_RANGE) / scorePerBall);
        }
    }
    
    averageReaction = sum / ballsEaten;
    averageBalls = 1000 / averageReaction;
    

    std::cout << "\n======================================\n";
    std::cout << "\nTime:\t\t" << elapsedTime << '\n';
    std::cout << "Avg. Balls/s:\t" << averageBalls << '\n';
    std::cout << "Avg. React:\t" << averageReaction << '\n';
    std::cout << "Balls:\t\t" << ballsEaten << '\n';
    std::cout << "Score:\t\t" << score << '\n';
    if (maxEat > 0.0)
        std::cout << "Max Eat:\t" << maxEat << " (Fastest 5s segment : " << 1000.0 / (maxEat / scorePerBall) << " ms cheese corrected avg., " << trueMaxEat << " ms true avg.)\n";
    else
        std::cout << "Max Eat:\t" << "Last ball hit was <5s, Max Eat measures fastest 5s segment" << '\n';
    std::cout << "Avg. Eat:\t" << averageEat << '\n';

    gameState = POSTGAME;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); 
}

void resetStats(GLFWwindow* window)
{
    if (!show_settings_windows)
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    reactionTimes.clear();
    eatTimes.clear();
    startingTime = 0.0;
    score = 0.0;
    maxEat = 0.0;
    trueMaxEat = 0.0;
    hp = startingHp;
    targetPos = glm::vec3(SCR_WIDTH / (2 * zoom), SCR_HEIGHT / (2 * zoom), 0.0f);
    gameState = PREGAME;
}

bool valueInCircle(float x1, float y1, float x2, float y2, float R)
{
    return std::sqrt(std::pow((x1 - x2),2) + std::pow((y1 - y2),2)) < R;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        restartGame(window);
    }
    if (key == GLFW_KEY_M && action == GLFW_PRESS)
    {
        show_settings_windows = !show_settings_windows;
        if (show_settings_windows)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else if (gameState != POSTGAME)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); 
        }
    }
    if (key == GLFW_KEY_O && action == GLFW_PRESS)
    {
        axiaCursor = !axiaCursor;
    }
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);

    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    
    updateRNG(distrib_x, distrib_y, width, height, zoom, targetSize.y, targetSize.x);
}

// glfw: whenever the mouse moves, this callback is called
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    cursorPos.x = ((float)xpos / zoom);
    cursorPos.y = ((float)ypos / zoom);
}

void updateRNG(std::uniform_int_distribution<>& xdist, std::uniform_int_distribution<>& ydist, int width, int height, float zoom, float tsy, float tsx)
{
    int lb_x = (((width / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (tsx);
    int ub_x = (((width / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (tsx);

    int lb_y = (((height / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (tsy) + 30.0f;
    int ub_y = (((height / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (tsy) - 30.0f;

    xdist.param(std::uniform_int_distribution<>::param_type(lb_x, ub_x));
    ydist.param(std::uniform_int_distribution<>::param_type(lb_y, ub_y));
}

HRESULT FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
    HRESULT hr = S_OK;
    if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD dwChunkType;
    DWORD dwChunkDataSize;
    DWORD dwRIFFDataSize = 0;
    DWORD dwFileType;
    DWORD bytesRead = 0;
    DWORD dwOffset = 0;

    while (hr == S_OK)
    {
        DWORD dwRead;
        if (0 == ReadFile(hFile, &dwChunkType, sizeof(DWORD), &dwRead, NULL))
            hr = HRESULT_FROM_WIN32(GetLastError());

        if (0 == ReadFile(hFile, &dwChunkDataSize, sizeof(DWORD), &dwRead, NULL))
            hr = HRESULT_FROM_WIN32(GetLastError());

        switch (dwChunkType)
        {
        case fourccRIFF:
            dwRIFFDataSize = dwChunkDataSize;
            dwChunkDataSize = 4;
            if (0 == ReadFile(hFile, &dwFileType, sizeof(DWORD), &dwRead, NULL))
                hr = HRESULT_FROM_WIN32(GetLastError());
            break;

        default:
            if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, dwChunkDataSize, NULL, FILE_CURRENT))
                return HRESULT_FROM_WIN32(GetLastError());
        }

        dwOffset += sizeof(DWORD) * 2;

        if (dwChunkType == fourcc)
        {
            dwChunkSize = dwChunkDataSize;
            dwChunkDataPosition = dwOffset;
            return S_OK;
        }

        dwOffset += dwChunkDataSize;

        if (bytesRead >= dwRIFFDataSize) return S_FALSE;

    }

    return S_OK;

}

HRESULT ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
    HRESULT hr = S_OK;
    if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, bufferoffset, NULL, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD dwRead;
    if (0 == ReadFile(hFile, buffer, buffersize, &dwRead, NULL))
        hr = HRESULT_FROM_WIN32(GetLastError());
    return hr;
}

void playSource(IXAudio2SourceVoice* voice, XAUDIO2_BUFFER* buffer)
{
    //Playing again without stopping is invalid on XAudio2
    voice->Stop(0); //Or use XAUDIO2_PLAY_TAILS for playing the reverb's tail
    //Remove the buffers and reset the audio position
    voice->FlushSourceBuffers();
    //Submit the buffer after the reset
    voice->SubmitSourceBuffer(buffer, nullptr);
    //Play
    voice->Start(0);
}