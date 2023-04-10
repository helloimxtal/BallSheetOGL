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

#define CURL_STATICLIB
#include "curl/curl.h"
#pragma comment (lib,"Normaliz.lib")
#pragma comment (lib,"Ws2_32.lib")
#pragma comment (lib,"Wldap32.lib")
#pragma comment (lib,"Crypt32.lib")

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
void updateRNG();
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

// Update checking
constexpr std::string VERSION_NAME = "v1.0.7";
enum UpdateResponse { OUTDATED = 0, UPTODATE = 1, BADQUERY = 2 };
UpdateResponse updateResponse = BADQUERY;

// Curl callback and helper
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
UpdateResponse checkLatestVersion();

// Play area constexprs
constexpr glm::vec3 outerQuadScale(800.0f, 800.0f, 1.0f);
constexpr glm::vec3 innerQuadScale(800.0f - (800.0f / 3.0f), 800.0f - (800.0f / 3.0f), 1.0f);

// Score constexprs
constexpr double MAX_EPS_RANGE = 5.0;

// Globals because I can't specify GLFW callback arguments (Although updateRNG and the RNG globals are only a thing because I fucked up the ortho zoom shit)

// Screen globals
int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;

// ImGui globals
bool show_settings_windows = true;
float zoom = 1.00f;
bool axiaCursor = false;

// Target / Cursor globals
glm::vec3 cursorPos(0.0f, 0.0f, 0.0f);
glm::vec3 targetPos(SCR_WIDTH / (2 * zoom), SCR_HEIGHT / (2 * zoom), 0.0f);

// Score globals
double elapsedTime = 0.0;
double averageBalls = 0.0;
double averageReaction = 0.0;
int ballsEaten = 0;
double maxEat = 0.0;
double trueMaxEat = 0.0;
double averageEat = 0.0;
double score = 0.0;

// Logic settings globals
glm::vec3 cursorSize(30.0f, 30.0f, 1.0f);
glm::vec3 targetSize(60.0f, 60.0f, 1.0f);
double scorePerBall = 35.0;
double PRESSURE = 60.0;
double startingHp = 100.0;
double cheeseThreshold = 100.0;

// Results screen globals
float cs_results{};
float ts_results{};
double spb_results{};
double p_results{};
double shp_results{};
double ct_results{};
bool changedSettings = false;

// Logic globals
double startingTime = 0.0;
double hp = startingHp;
std::vector<double> reactionTimes{};
std::vector<double> eatTimes{}; // Need this for ImPlot
enum GameStates { INGAME = 0, PREGAME = 1, POSTGAME = 2 };
GameStates gameState = PREGAME;

// Old style reset with optional flash delay for screen recordings
struct FastReset {
    bool flash;
    bool requested;
    double time;
    void request() { requested = true; time = glfwGetTime(); };
    void clear() { requested = false; };
};
FastReset fastReset{ true, false, 0.0 };

// RNG globals
std::random_device rd;
std::mt19937 gen(rd());
float lb_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.x);
float ub_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.x);
float lb_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.y) + 30.0f;
float ub_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.y) - 30.0f;
std::uniform_real_distribution<float> distrib_x(lb_x, ub_x);
std::uniform_real_distribution<float> distrib_y(lb_y, ub_y);

int main()
{
    // Rate limits at 60 requests/hour, apparently per User Agent (which is just BallSheetOGL/versionNumber)
    updateResponse = checkLatestVersion();

    // glfw boilerplate
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    std::string titleName = "BallSheetOGL " + VERSION_NAME;
    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, titleName.c_str(), NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

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

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    glfwSetWindowMonitor(window, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height, mode->refreshRate);

    // Begin VAO setup
    
    float vertices[] = 
    {
        // pos       // tex
        0.0f, 1.0f,  0.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        0.0f, 0.0f,  0.0f, 0.0f,      
        1.0f, 1.0f,  1.0f, 1.0f,
    };
    
    GLuint indices[] =
    {
        0, 1, 2,
        0, 3, 1
    };

    GLuint VBO;
    GLuint EBO;
    GLuint quadVAO;

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // End VAO setup

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
    glm::vec4 clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 target_color(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 cursor_color(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 inner_quad_color(25.0f / 255.0f, 25.0f / 255.0f, 25.0f / 255.0f, 1.0f);
    glm::vec4 outer_quad_color(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f);
    double deltaTime = 0.0;
    double lastFrame = 0.0;
    double lastHit = 0.0;

    // Prevent vectors from relocating until Axia/Lorenzo get a 100s score
    reactionTimes.reserve(1024);
    eatTimes.reserve(1024);

    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(quadVAO);

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
            if (gameState == INGAME) // Eat a ball
            {
                reactionTimes.emplace_back(currentFrame - lastHit);
                eatTimes.emplace_back(currentFrame);
                double eat = scorePerBall * ((std::min)(reactionTimes.back(), (cheeseThreshold / 1000.0)) / (cheeseThreshold / 1000.0));
                score += eat;
                hp += eat;
                lastHit = currentFrame;
            }
            else if (gameState == PREGAME) // Start the game
            {
                gameState = INGAME;
                startingTime = currentFrame;
                lastHit = 0.0;
                lastFrame = 0.0;
                score = 0.0;
                hp = startingHp;
                changedSettings = false;
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
        // Shader binding was here
        glm::mat4 projection = glm::ortho(0.0f, (float)SCR_WIDTH / zoom, (float)SCR_HEIGHT / zoom, 0.0f, -1.0f, 1.0f);
        shader.setMat4("projection", projection);

        // VAO Binding was here
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
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // Inner quad
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(
            (((float)SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f),
            (((float)SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f),
            0.0f));
        model = glm::scale(model, innerQuadScale);
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", inner_quad_color * inner_quad_color.w);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

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
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // Target
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(targetPos.x - (targetSize.x / 2), targetPos.y - (targetSize.y / 2), 0.0f));
        model = glm::scale(model, targetSize);
        shader.setMat4("model", model);
        shader.setVec3("spriteColor", target_color * target_color.w);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

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
                float oldcs = cursorSize.x;
                float oldts = targetSize.x;
                double oldspb = scorePerBall;
                double oldp = PRESSURE;
                double oldshp = startingHp;
                double oldct = cheeseThreshold;

                ImGui::Begin("Settings");

                if (updateResponse == OUTDATED)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("OUTDATED VERSION, GO TO github.com/helloimxtal/BallSheetOGL/releases");
                    ImGui::Text(std::string("Current version: " + VERSION_NAME).c_str());
                    ImGui::PopStyleColor();
                }
                else if (updateResponse == UPTODATE)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                    ImGui::Text(std::string("On the latest version (" + VERSION_NAME + ")").c_str());
                    ImGui::PopStyleColor();
                }
                else if (updateResponse == BADQUERY)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
                    ImGui::Text("Failed to check if on latest version, likely rate limited");
                    ImGui::Text(std::string("Current version: " + VERSION_NAME).c_str());
                    ImGui::PopStyleColor();
                }

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

                ImGui::NewLine();

                ImGui::Text("R = fast reset, E = hold stats screen");
                ImGui::Checkbox("Flash results on R press", &fastReset.flash);

                ImGui::NewLine();

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::End();

                cursorSize.y = cursorSize.x;
                targetSize.y = targetSize.x;

                if (oldZoom != zoom || oldts != targetSize.x)
                {
                    updateRNG();
                    targetPos.x += ((SCR_WIDTH / zoom) - (SCR_WIDTH / oldZoom)) / 2.0f;
                    targetPos.y += ((SCR_HEIGHT / zoom) - (SCR_HEIGHT / oldZoom)) / 2.0f;
                }

                // This is gross but I justify it knowing that you aren't actually going for a "real" run when you leave the menu open
                // Obviously I could just close the settings window during the run but I would find that annoying + nobody said they want that
                if (gameState == INGAME && (oldcs != cursorSize.x || oldts != targetSize.x || oldspb != scorePerBall || oldp != PRESSURE || oldshp != startingHp || oldct != cheeseThreshold))
                {
                    changedSettings = true;
                }
            }
        }

        if (fastReset.requested && (glfwGetTime() - fastReset.time > (1.0 / 60.0) || !fastReset.flash)) // flashes for 1x 60 fps frame
        {
            restartGame(window);
        }

        if (gameState == POSTGAME)
        {
            ImGui::SetNextWindowPos(ImVec2(((float)SCR_WIDTH - 800.0f) / 2.0f, ((float)SCR_HEIGHT - 800.0f) / 2.0f));
            ImGui::SetNextWindowSize(ImVec2(outerQuadScale.x, outerQuadScale.y));
            ImPlot::SetNextAxesToFit();
            ImGui::Begin("Results");
            if (ImPlot::BeginPlot("Results"))
            {
                ImPlot::SetupAxes("Elapsed Time", "Reaction Time");
                ImPlot::PlotLine("Graph", &eatTimes.at(0), &reactionTimes.at(0), reactionTimes.size());
                ImPlot::EndPlot();

                if (changedSettings)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("Settings were changed mid run");
                    ImGui::NewLine();
                    ImGui::PopStyleColor();
                }
                ImGui::Text("Time:         %f", elapsedTime);
                ImGui::Text("Avg. Balls/s: %f", averageBalls);
                ImGui::Text("Avg. React:   %f (Untrimmed)", averageReaction);
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

                ImGui::Text("Cursor Size:      %f", cs_results);
                ImGui::Text("Target Size:      %f", ts_results);
                ImGui::Text("Score Per Ball:   %f", spb_results);
                ImGui::Text("Pressure:         %f", p_results);
                ImGui::Text("Starting Hp:      %f", shp_results);
                ImGui::Text("Cheese Threshold: %f", ct_results);

                ImGui::NewLine();

                ImGui::Text("Press R or E to close this window");
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
    if (reactionTimes.size() == 0 || gameState == POSTGAME)
    {
        resetStats(window);
        return;
    }

    // Present the settings finished with
    cs_results = cursorSize.x;
    ts_results = targetSize.x;
    spb_results = scorePerBall;
    p_results = PRESSURE;
    shp_results = startingHp;
    ct_results = cheeseThreshold;

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
    for (int i = 0; eatTimes.at(i) <= eatTimes.back() - MAX_EPS_RANGE; i++)
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
    
    
    std::cout << "\n======================================\n\n";
    if (changedSettings) { std::cout << "Settings were changed mid run\n\n"; }
    std::cout << "Time:         " << elapsedTime << '\n';
    std::cout << "Avg. Balls/s: " << averageBalls << '\n';
    std::cout << "Avg. React:   " << averageReaction << '\n';
    std::cout << "Balls:        " << ballsEaten << '\n';
    std::cout << "Score:        " << score << '\n';
    if (maxEat > 0.0)
        std::cout << "Max Eat:      " << maxEat << " (Fastest 5s segment : " << 1000.0 / (maxEat / scorePerBall) << " ms cheese corrected avg., " << trueMaxEat << " ms true avg.)\n";
    else
        std::cout << "Max Eat:      " << "Last ball hit was <5s, Max Eat measures fastest 5s segment" << '\n';
    std::cout << "Avg. Eat:     " << averageEat << '\n';

    std::cout << "\nCursor Size:      " << cs_results << '\n';
    std::cout << "Target Size:      " << ts_results << '\n';
    std::cout << "Score Per Ball:   " << spb_results << '\n';
    std::cout << "Pressure:         " << p_results << '\n';
    std::cout << "Starting Hp:      " << shp_results << '\n';
    std::cout << "Cheese Threshold: " << ct_results << '\n';

    gameState = POSTGAME;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); 
}

void resetStats(GLFWwindow* window)
{
    if (!show_settings_windows)
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    if (fastReset.requested)
        fastReset.clear();
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
        fastReset.request();
        restartGame(window);
    }
    if (key == GLFW_KEY_E && action == GLFW_PRESS)
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
    if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
    {
        if (glfwGetWindowMonitor(window) == NULL) // windowed
        {
            const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
            glfwSetWindowMonitor(window, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else // fullscreen
        {
            glfwSetWindowMonitor(window, NULL, 160, 90, 1600, 900, 0);
        }
    }
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);

    targetPos.x += ((width / zoom)  - (SCR_WIDTH / zoom)) / 2.0f;
    targetPos.y += ((height / zoom) - (SCR_HEIGHT / zoom)) / 2.0f;

    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    
    updateRNG();
}

// glfw: whenever the mouse moves, this callback is called
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    cursorPos.x = ((float)xpos / zoom);
    cursorPos.y = ((float)ypos / zoom);
}

void updateRNG()
{
    float lb_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.x);
    float ub_x = (((SCR_WIDTH / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.x);

    float lb_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (1.0f / 6.0f)) + (targetSize.y) + 30.0f;
    float ub_y = (((SCR_HEIGHT / zoom - 800.0f) / 2.0f) + 800.0f * (5.0f / 6.0f)) - (targetSize.y) - 30.0f;

    distrib_x.param(std::uniform_real_distribution<float>::param_type(lb_x, ub_x));
    distrib_y.param(std::uniform_real_distribution<float>::param_type(lb_y, ub_y));
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

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

UpdateResponse checkLatestVersion()
{
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    std::string USER_AGENT = "BallSheetOGL/" + VERSION_NAME;
    std::cout << USER_AGENT << '\n';

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/helloimxtal/BallSheetOGL/releases/latest");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
    }

    size_t beginPos = readBuffer.find("\"tag_name\":");
    if (beginPos != std::string::npos)
    {
        beginPos += std::string("\"tag_name\":").length();
        size_t endPos = readBuffer.find(',', beginPos);
        std::string tagName = readBuffer.substr(beginPos + 1, endPos - beginPos - 2);
        std::cout << "Current version: " << VERSION_NAME << '\n';
        std::cout << "Latest version: " << tagName << '\n';
        if (tagName == VERSION_NAME)
        {
            return UPTODATE;
        } 
        else
        {
            return OUTDATED;
        }  
    }
    return BADQUERY;
}