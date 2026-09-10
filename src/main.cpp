#define SDL_MAIN_USE_CALLBACKS 1

#include "webgpu_context.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cglm/cglm.h>
#include <stdio.h>
#include <webgpu/webgpu.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_wgpu.h"

static SDL_Window *window = NULL;

static WGPURenderPipeline pipeline = NULL;
static WGPUBuffer uniform_buffer = NULL;
static WGPUBindGroup bind_group = NULL;

static bool imgui_initialized = false;

// Transform & Color state
typedef struct TriangleTransform
{
    vec2 pos;
    float rotation; // In degrees
    vec2 scale;
} TriangleTransform;

static TriangleTransform tri_transform = {
    { 0.0f, 0.0f },    // Center position
    0.0f,             // 0 degrees rotation
    { 150.0f, 150.0f } // Width and Height in pixels
};

static float triangle_color[4] = { 1.0f, 0.5f, 0.0f, 1.0f }; // Default orange

// Uniform structure containing Model-View-Projection matrix & Color
struct Uniforms
{
    mat4 mvp;
    vec4 color;
};

typedef struct Uniforms Uniforms;

static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

static const char *shader_code =
    "struct Uniforms {\n"
    "    mvp: mat4x4f,\n"
    "    color: vec4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4f,\n"
    "};\n\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> VertexOutput {\n"
    "    var pos = array<vec2f, 3>(\n"
    "        vec2f(0.0, 0.5),\n"
    "        vec2f(-0.5, -0.5),\n"
    "        vec2f(0.5, -0.5)\n"
    "    );\n"
    "    var out: VertexOutput;\n"
    "    out.position = u.mvp * vec4f(pos[in_vertex_index], 0.0, 1.0);\n"
    "    return out;\n"
    "}\n\n"
    "@fragment\n"
    "fn fs_main() -> @location(0) vec4f {\n"
    "    return u.color;\n"
    "}\n";

static bool InitPipeline(void)
{
    LogApp(">>> Creating Uniform Buffer & Bind Groups");

    // Create Uniform Buffer
    WGPUBufferDescriptor buffer_desc = {};
    buffer_desc.size = sizeof(Uniforms);
    buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniform_buffer = wgpuDeviceCreateBuffer(g_gpu.device, &buffer_desc);

    // Bind Group Layout
    WGPUBindGroupLayoutEntry bgl_entry = {};
    bgl_entry.binding = 0;
    bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entry.buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc = {};
    bgl_desc.entryCount = 1;
    bgl_desc.entries = &bgl_entry;
    WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(g_gpu.device, &bgl_desc);

    // Create Bind Group
    WGPUBindGroupEntry bg_entry = {};
    bg_entry.binding = 0;
    bg_entry.buffer = uniform_buffer;
    bg_entry.size = sizeof(Uniforms);

    WGPUBindGroupDescriptor bg_desc = {};
    bg_desc.layout = bind_group_layout;
    bg_desc.entryCount = 1;
    bg_desc.entries = &bg_entry;
    bind_group = wgpuDeviceCreateBindGroup(g_gpu.device, &bg_desc);

    // Create Pipeline Layout
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {};
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(g_gpu.device, &pipeline_layout_desc);

    // Create Shader & Pipeline
    WGPUShaderSourceWGSL wgsl_desc = {};
    wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_desc.code = WGPU_STR(shader_code);

    WGPUShaderModuleDescriptor shader_desc = {};
    shader_desc.nextInChain = &wgsl_desc.chain;
    WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(g_gpu.device, &shader_desc);

    WGPUBlendState blend_state = {};
    blend_state.color.operation = WGPUBlendOperation_Add;
    blend_state.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend_state.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend_state.alpha.operation = WGPUBlendOperation_Add;
    blend_state.alpha.srcFactor = WGPUBlendFactor_One;
    blend_state.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState color_target = {};
    color_target.format = g_gpu.config.format;
    color_target.blend = &blend_state;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = {};
    fragment_state.module = shader_module;
    fragment_state.entryPoint = WGPU_STR("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPURenderPipelineDescriptor pipeline_desc = {};
    pipeline_desc.layout = pipeline_layout;
    pipeline_desc.vertex.module = shader_module;
    pipeline_desc.vertex.entryPoint = WGPU_STR("vs_main");
    pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline_desc.primitive.frontFace = WGPUFrontFace_CCW;
    pipeline_desc.primitive.cullMode = WGPUCullMode_None;

    pipeline_desc.multisample.count = 1;
    pipeline_desc.multisample.mask = 0xFFFFFFFF;
    pipeline_desc.multisample.alphaToCoverageEnabled = false;

    pipeline_desc.fragment = &fragment_state;

    pipeline = wgpuDeviceCreateRenderPipeline(g_gpu.device, &pipeline_desc);

    wgpuBindGroupLayoutRelease(bind_group_layout);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader_module);

    // Init ImGui WebGPU renderer backend
    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = g_gpu.device;
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = g_gpu.config.format;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    imgui_initialized = true;

    return pipeline != NULL;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
#ifndef __EMSCRIPTEN__
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO))
        return SDL_APP_FAILURE;

    window = SDL_CreateWindow(
        "WebGPU cglm Triangle + ImGui",
        WIN_WIDTH,
        WIN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!window)
    {
        return SDL_APP_FAILURE;
    }

    if (!InitWebGPUContext(&g_gpu, window))
    {
        return SDL_APP_FAILURE;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    // Query native display scale from SDL3
    float scale = 1.0f;
#if !defined(__EMSCRIPTEN__)
    scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f)
        scale = 1.0f;
#endif

    // Scale UI element paddings, margins, scrollbars
    ImGui::GetStyle().ScaleAllSizes(scale);

    // Load scaled default font
    ImFontConfig font_cfg;
    font_cfg.SizePixels = 15.0f * scale;
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui_ImplSDL3_InitForOther(window);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    ImGui_ImplSDL3_ProcessEvent(event);

    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        g_gpu.need_reconfigure = true;
    }

    if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        g_gpu.need_recreate_surface = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (g_gpu.instance)
    {
        wgpuInstanceProcessEvents(g_gpu.instance);
    }

    if (!g_gpu.device)
    {
        return SDL_APP_CONTINUE;
    }

    bool was_configured = g_gpu.is_configured;

    ReconfigureSurfaceIfNeeded(&g_gpu);

    if (!was_configured && g_gpu.is_configured)
    {
        if (!InitPipeline())
        {
            return SDL_APP_FAILURE;
        }
    }

    WGPUSurfaceTexture surfaceTexture = {};
    wgpuSurfaceGetCurrentTexture(g_gpu.surface, &surfaceTexture);

#if defined(WGPUSurfaceGetCurrentTextureStatus_Success) && defined(WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#define SURFACE_STATUS_SUCCESS(s) \
    ((s) == WGPUSurfaceGetCurrentTextureStatus_Success || (s) == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#elif defined(WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#define SURFACE_STATUS_SUCCESS(s) ((s) == WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#else
#define SURFACE_STATUS_SUCCESS(s) ((s) == 0 || (s) == 1)
#endif

    if (!SURFACE_STATUS_SUCCESS(surfaceTexture.status))
    {
        return SDL_APP_CONTINUE;
    }

    int w_pixels = 0, h_pixels = 0;
    SDL_GetWindowSizeInPixels(window, &w_pixels, &h_pixels);
    if (w_pixels <= 0 || h_pixels <= 0)
    {
        return SDL_APP_CONTINUE;
    }

    // Start ImGui Frame
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Query screen dimensions for UI positioning
    ImGuiIO &io = ImGui::GetIO();

    float display_scale = 1.0f;
#if !defined(__EMSCRIPTEN__)
    display_scale = SDL_GetWindowDisplayScale(window);
    if (display_scale <= 0.0f)
        display_scale = 1.0f;
#endif

    float max_card_width = 360.0f * display_scale;
    float desired_width = (io.DisplaySize.x * 0.90f < max_card_width) ? (io.DisplaySize.x * 0.90f) : max_card_width;

    float pos_x = 20.0f * display_scale;
    float pos_y = 20.0f * display_scale;

    ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(desired_width, 0.0f), ImGuiCond_Always);

    ImGui::Begin("Triangle Controls", nullptr, ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushItemWidth(-FLT_MIN);

    ImGui::Text("Position (X, Y)");
    ImGui::SliderFloat2("##Position", tri_transform.pos, -((float)w_pixels * 0.5f), (float)w_pixels * 0.5f);

    ImGui::Spacing();
    ImGui::Text("Scale (Width, Height)");
    ImGui::SliderFloat2("##Scale", tri_transform.scale, 10.0f, 600.0f);

    ImGui::Spacing();
    ImGui::Text("Rotation (Degrees)");
    ImGui::SliderFloat("##Rotation", &tri_transform.rotation, -180.0f, 180.0f);

    ImGui::Spacing();
    ImGui::Text("Color");
    ImGui::ColorEdit4("##Color", triangle_color);

    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (ImGui::Button("Reset Transform", ImVec2(-FLT_MIN, 0.0f)))
    {
        tri_transform.pos[0] = 0.0f;
        tri_transform.pos[1] = 0.0f;
        tri_transform.scale[0] = 150.0f;
        tri_transform.scale[1] = 150.0f;
        tri_transform.rotation = 0.0f;
        triangle_color[0] = 1.0f;
        triangle_color[1] = 0.5f;
        triangle_color[2] = 0.0f;
        triangle_color[3] = 1.0f;
    }

    ImGui::End();

    // 1. Projection matrix using cglm orthographic projection
    mat4 proj;
    float half_w = (float)w_pixels * 0.5f;
    float half_h = (float)h_pixels * 0.5f;
    glm_ortho(-half_w, half_w, half_h, -half_h, -1.0f, 1.0f, proj);

    // 2. Model matrix with position, rotation, and scaling using cglm
    vec3 scaled_pos = {
        tri_transform.pos[0] * display_scale,
        tri_transform.pos[1] * display_scale,
        0.0f
    };

    vec3 scaled_size = {
        tri_transform.scale[0] * display_scale,
        tri_transform.scale[1] * display_scale,
        1.0f
    };

    vec3 axis_z = { 0.0f, 0.0f, 1.0f };

    mat4 model = GLM_MAT4_IDENTITY_INIT;
    glm_translate(model, scaled_pos);
    glm_rotate(model, glm_rad(tri_transform.rotation), axis_z);
    glm_scale(model, scaled_size);

    // 3. Compute MVP matrix and fill uniform data
    Uniforms uniforms = {};
    glm_mat4_mul(proj, model, uniforms.mvp);

    glm_vec4_copy(triangle_color, uniforms.color);

    wgpuQueueWriteBuffer(g_gpu.queue, uniform_buffer, 0, &uniforms, sizeof(Uniforms));

    // Render WebGPU frame
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_gpu.device, NULL);

    WGPUColor clear_color = { 0.15, 0.15, 0.18, 1.0 };
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = view;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = clear_color;

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Draw Triangle
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    // Draw ImGui UI
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);

    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(g_gpu.queue, 1, &commandBuffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g_gpu.surface);
#endif

    if (commandBuffer)
        wgpuCommandBufferRelease(commandBuffer);
    if (pass)
        wgpuRenderPassEncoderRelease(pass);
    if (encoder)
        wgpuCommandEncoderRelease(encoder);
    if (view)
        wgpuTextureViewRelease(view);
    if (surfaceTexture.texture)
        wgpuTextureRelease(surfaceTexture.texture);

#undef SURFACE_STATUS_SUCCESS

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (imgui_initialized)
    {
        ImGui_ImplWGPU_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (bind_group)
        wgpuBindGroupRelease(bind_group);
    if (uniform_buffer)
        wgpuBufferRelease(uniform_buffer);
    if (pipeline)
        wgpuRenderPipelineRelease(pipeline);

    DestroyWebGPUContext(&g_gpu);

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}
