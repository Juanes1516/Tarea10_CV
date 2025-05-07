/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "Tutorial21_RayTracing.hpp"
#include "MapHelper.hpp"
#include "GraphicsTypesX.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "AdvancedMath.hpp"
#include "PlatformMisc.hpp"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new Tutorial21_RayTracing();
}


void Tutorial21_RayTracing::CreateGraphicsPSO()
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name                                  = "Image blit PSO";
    PSOCreateInfo.PSODesc.PipelineType                          = PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags   = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Image blit VS";
        ShaderCI.FilePath        = "ImageBlit.vsh";
        m_pDevice->CreateShader(ShaderCI, &pVS);
        VERIFY_EXPR(pVS != nullptr);
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Image blit PS";
        ShaderCI.FilePath        = "ImageBlit.psh";
        m_pDevice->CreateShader(ShaderCI, &pPS);
        VERIFY_EXPR(pPS != nullptr);
    }

    PSOCreateInfo.pVS                                        = pVS;
    PSOCreateInfo.pPS                                        = pPS;
    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pImageBlitPSO);
    VERIFY_EXPR(m_pImageBlitPSO != nullptr);
    m_pImageBlitPSO->CreateShaderResourceBinding(&m_pImageBlitSRB, true);
    VERIFY_EXPR(m_pImageBlitSRB != nullptr);
}

void Tutorial21_RayTracing::CreateRayTracingPSO()
{
    m_MaxRecursionDepth = std::min(m_MaxRecursionDepth, m_pDevice->GetAdapterInfo().RayTracing.MaxRecursionDepth);

    RayTracingPipelineStateCreateInfoX PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = "Ray tracing PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;

    ShaderMacroHelper Macros;
    Macros.AddShaderMacro("NUM_TEXTURES", NumTextures);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.UseCombinedTextureSamplers = false;
    ShaderCI.Macros                          = Macros;
    ShaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags                    = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.HLSLVersion                     = {6, 3};
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pRayGen;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_GEN;
        ShaderCI.Desc.Name       = "Ray tracing RG";
        ShaderCI.FilePath        = "RayTrace.rgen";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pRayGen);
        VERIFY_EXPR(pRayGen != nullptr);
    }

    RefCntAutoPtr<IShader> pPrimaryMiss, pShadowMiss;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
        ShaderCI.Desc.Name       = "Primary ray miss shader";
        ShaderCI.FilePath        = "PrimaryMiss.rmiss";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pPrimaryMiss);
        VERIFY_EXPR(pPrimaryMiss != nullptr);

        ShaderCI.Desc.Name  = "Shadow ray miss shader";
        ShaderCI.FilePath   = "ShadowMiss.rmiss";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pShadowMiss);
        VERIFY_EXPR(pShadowMiss != nullptr);
    }

    RefCntAutoPtr<IShader> pCubePrimaryHit, pGroundHit, pGlassPrimaryHit, pSpherePrimaryHit;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
        ShaderCI.Desc.Name       = "Cube primary ray closest hit shader";
        ShaderCI.FilePath        = "CubePrimaryHit.rchit";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pCubePrimaryHit);
        VERIFY_EXPR(pCubePrimaryHit != nullptr);

        ShaderCI.Desc.Name  = "Ground primary ray closest hit shader";
        ShaderCI.FilePath   = "Ground.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pGroundHit);
        VERIFY_EXPR(pGroundHit != nullptr);

        ShaderCI.Desc.Name  = "Glass primary ray closest hit shader";
        ShaderCI.FilePath   = "GlassPrimaryHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pGlassPrimaryHit);
        VERIFY_EXPR(pGlassPrimaryHit != nullptr);

        ShaderCI.Desc.Name  = "Sphere primary ray closest hit shader";
        ShaderCI.FilePath   = "SpherePrimaryHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pSpherePrimaryHit);
        VERIFY_EXPR(pSpherePrimaryHit != nullptr);
    }

    RefCntAutoPtr<IShader> pSphereIntersection;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_INTERSECTION;
        ShaderCI.Desc.Name       = "Sphere intersection shader";
        ShaderCI.FilePath        = "SphereIntersection.rint";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pSphereIntersection);
        VERIFY_EXPR(pSphereIntersection != nullptr);
    }

    PSOCreateInfo.AddGeneralShader("Main", pRayGen);
    PSOCreateInfo.AddGeneralShader("PrimaryMiss", pPrimaryMiss);
    PSOCreateInfo.AddGeneralShader("ShadowMiss", pShadowMiss);

    PSOCreateInfo.AddTriangleHitShader("CubePrimaryHit", pCubePrimaryHit);
    PSOCreateInfo.AddTriangleHitShader("GroundHit", pGroundHit);
    PSOCreateInfo.AddTriangleHitShader("GlassPrimaryHit", pGlassPrimaryHit);

    PSOCreateInfo.AddProceduralHitShader("SpherePrimaryHit", pSphereIntersection, pSpherePrimaryHit);
    PSOCreateInfo.AddProceduralHitShader("SphereShadowHit", pSphereIntersection);

    PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = static_cast<Uint8>(m_MaxRecursionDepth);
    PSOCreateInfo.RayTracingPipeline.ShaderRecordSize  = 0;
    PSOCreateInfo.MaxAttributeSize                     = std::max<Uint32>(sizeof(float2), sizeof(HLSL::ProceduralGeomIntersectionAttribs));
    PSOCreateInfo.MaxPayloadSize                       = std::max<Uint32>(sizeof(HLSL::PrimaryRayPayload), sizeof(HLSL::ShadowRayPayload));

    SamplerDesc SamLinearWrapDesc{
        FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
        TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP};

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout.AddImmutableSampler(SHADER_TYPE_RAY_CLOSEST_HIT, "g_SamLinearWrap", SamLinearWrapDesc);
    ResourceLayout
        .AddVariable(SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS | SHADER_TYPE_RAY_CLOSEST_HIT,
                     "g_ConstantsCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_GEN, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    m_pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &m_pRayTracingPSO);
    VERIFY_EXPR(m_pRayTracingPSO != nullptr);

    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "g_ConstantsCB")->Set(m_ConstantsCB);
    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "g_ConstantsCB")->Set(m_ConstantsCB);
    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_ConstantsCB")->Set(m_ConstantsCB);

    m_pRayTracingPSO->CreateShaderResourceBinding(&m_pRayTracingSRB, true);
    VERIFY_EXPR(m_pRayTracingSRB != nullptr);
}

void Tutorial21_RayTracing::LoadTextures()
{
    IDeviceObject*          pTexSRVs[NumTextures] = {};
    RefCntAutoPtr<ITexture> pTex[NumTextures];
    StateTransitionDesc     Barriers[NumTextures];
    for (int tex = 0; tex < NumTextures; ++tex)
    {
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB = true;
        std::stringstream ss;
        ss << "DGLogo" << tex << ".png";
        CreateTextureFromFile(ss.str().c_str(), loadInfo, m_pDevice, &pTex[tex]);
        ITextureView* srv = pTex[tex]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        pTexSRVs[tex]     = srv;
        Barriers[tex]     = StateTransitionDesc{pTex[tex], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
    }
    m_pImmediateContext->TransitionResourceStates(_countof(Barriers), Barriers);
    m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_CubeTextures")->SetArray(pTexSRVs, 0, NumTextures);

    RefCntAutoPtr<ITexture> pGroundTex;
    CreateTextureFromFile("Ground.jpg", TextureLoadInfo{}, m_pDevice, &pGroundTex);
    m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_GroundTexture")
        ->Set(pGroundTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

void Tutorial21_RayTracing::CreateCubeBLAS()
{
    RefCntAutoPtr<IDataBlob> pCubeVerts, pCubeIndices;
    GeometryPrimitiveInfo    CubeGeoInfo;
    constexpr float          CubeSize = 2.f;
    CreateGeometryPrimitive(CubeGeometryPrimitiveAttributes{CubeSize, GEOMETRY_PRIMITIVE_VERTEX_FLAG_ALL},
                            &pCubeVerts, &pCubeIndices, &CubeGeoInfo);

    struct CubeVertex
    {
        float3 Pos;
        float3 Normal;
        float2 UV;
    };
    VERIFY_EXPR(CubeGeoInfo.VertexSize == sizeof(CubeVertex));
    const CubeVertex* pVerts   = pCubeVerts->GetConstDataPtr<CubeVertex>();
    const Uint32*     pIndices = pCubeIndices->GetConstDataPtr<Uint32>();

    {
        HLSL::CubeAttribs Attribs{};
        for (Uint32 v = 0; v < CubeGeoInfo.NumVertices; ++v)
        {
            Attribs.UVs[v]     = {pVerts[v].UV, 0, 0};
            Attribs.Normals[v] = pVerts[v].Normal;
        }
        for (Uint32 i = 0; i < CubeGeoInfo.NumIndices; i += 3)
        {
            const Uint32* tri         = &pIndices[i];
            Attribs.Primitives[i / 3] = uint4{tri[0], tri[1], tri[2], 0};
        }
        BufferDesc BuffDesc;
        BuffDesc.Name      = "Cube Attribs";
        BuffDesc.Usage     = USAGE_IMMUTABLE;
        BuffDesc.BindFlags = BIND_UNIFORM_BUFFER;
        BuffDesc.Size      = sizeof(Attribs);
        BufferData BufData = {&Attribs, BuffDesc.Size};
        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_CubeAttribsCB);
        VERIFY_EXPR(m_CubeAttribsCB);
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_CubeAttribsCB")
            ->Set(m_CubeAttribsCB);
    }

    RefCntAutoPtr<IBuffer>             pCubeVertexBuffer, pCubeIndexBuffer;
    GeometryPrimitiveBuffersCreateInfo CubeBuffersCI;
    CubeBuffersCI.VertexBufferBindFlags = BIND_RAY_TRACING;
    CubeBuffersCI.IndexBufferBindFlags  = BIND_RAY_TRACING;
    CreateGeometryPrimitiveBuffers(m_pDevice,
                                   CubeGeometryPrimitiveAttributes{CubeSize, GEOMETRY_PRIMITIVE_VERTEX_FLAG_POSITION},
                                   &CubeBuffersCI, &pCubeVertexBuffer, &pCubeIndexBuffer);

    {
        BLASTriangleDesc Tri;
        Tri.GeometryName         = "Cube";
        Tri.MaxVertexCount       = CubeGeoInfo.NumVertices;
        Tri.VertexValueType      = VT_FLOAT32;
        Tri.VertexComponentCount = 3;
        Tri.MaxPrimitiveCount    = CubeGeoInfo.NumIndices / 3;
        Tri.IndexType            = VT_UINT32;
        BottomLevelASDesc ASDesc;
        ASDesc.Name          = "Cube BLAS";
        ASDesc.Flags         = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
        ASDesc.pTriangles    = &Tri;
        ASDesc.TriangleCount = 1;
        m_pDevice->CreateBLAS(ASDesc, &m_pCubeBLAS);
        VERIFY_EXPR(m_pCubeBLAS);

        BufferDesc ScratchDesc;
        ScratchDesc.Name      = "BLAS Scratch Buffer";
        ScratchDesc.Usage     = USAGE_DEFAULT;
        ScratchDesc.BindFlags = BIND_RAY_TRACING;
        ScratchDesc.Size      = m_pCubeBLAS->GetScratchBufferSizes().Build;
        RefCntAutoPtr<IBuffer> pScratch;
        m_pDevice->CreateBuffer(ScratchDesc, nullptr, &pScratch);

        BLASBuildTriangleData TriData;
        TriData.GeometryName         = Tri.GeometryName;
        TriData.pVertexBuffer        = pCubeVertexBuffer;
        TriData.VertexStride         = sizeof(float3);
        TriData.VertexCount          = Tri.MaxVertexCount;
        TriData.VertexValueType      = Tri.VertexValueType;
        TriData.VertexComponentCount = Tri.VertexComponentCount;
        TriData.pIndexBuffer         = pCubeIndexBuffer;
        TriData.PrimitiveCount       = Tri.MaxPrimitiveCount;
        TriData.IndexType            = Tri.IndexType;
        TriData.Flags                = RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        BuildBLASAttribs Attribs;
        Attribs.pBLAS                       = m_pCubeBLAS;
        Attribs.pTriangleData               = &TriData;
        Attribs.TriangleDataCount           = 1;
        Attribs.pScratchBuffer              = pScratch;
        Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        m_pImmediateContext->BuildBLAS(Attribs);
    }
}

void Tutorial21_RayTracing::CreateProceduralBLAS()
{
    static_assert(sizeof(HLSL::BoxAttribs) % 16 == 0, "");
    const HLSL::BoxAttribs Boxes[] = {{-2.5f, -2.5f, -2.5f, 2.5f, 2.5f, 2.5f}};

    BufferDesc BoxDesc;
    BoxDesc.Name              = "AABB Buffer";
    BoxDesc.Usage             = USAGE_IMMUTABLE;
    BoxDesc.BindFlags         = BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
    BoxDesc.Size              = sizeof(Boxes);
    BoxDesc.ElementByteStride = sizeof(Boxes[0]);
    BoxDesc.Mode              = BUFFER_MODE_STRUCTURED;
    BufferData BoxData        = {Boxes, sizeof(Boxes)};
    m_pDevice->CreateBuffer(BoxDesc, &BoxData, &m_BoxAttribsCB);
    VERIFY_EXPR(m_BoxAttribsCB);
    m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_INTERSECTION, "g_BoxAttribs")
        ->Set(m_BoxAttribsCB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

    BLASBoundingBoxDesc BoxInfo;
    BoxInfo.GeometryName = "Box";
    BoxInfo.MaxBoxCount  = 1;
    BottomLevelASDesc ASDesc;
    ASDesc.Name     = "Procedural BLAS";
    ASDesc.Flags    = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
    ASDesc.pBoxes   = &BoxInfo;
    ASDesc.BoxCount = 1;
    m_pDevice->CreateBLAS(ASDesc, &m_pProceduralBLAS);
    VERIFY_EXPR(m_pProceduralBLAS);

    BufferDesc ScratchDesc;
    ScratchDesc.Name      = "BLAS Scratch Buffer";
    ScratchDesc.Usage     = USAGE_DEFAULT;
    ScratchDesc.BindFlags = BIND_RAY_TRACING;
    ScratchDesc.Size      = m_pProceduralBLAS->GetScratchBufferSizes().Build;
    RefCntAutoPtr<IBuffer> pScratch;
    m_pDevice->CreateBuffer(ScratchDesc, nullptr, &pScratch);

    BLASBuildBoundingBoxData BoxDataDesc;
    BoxDataDesc.GeometryName = BoxInfo.GeometryName;
    BoxDataDesc.BoxCount     = 1;
    BoxDataDesc.BoxStride    = sizeof(Boxes[0]);
    BoxDataDesc.pBoxBuffer   = m_BoxAttribsCB;

    BuildBLASAttribs Attribs;
    Attribs.pBLAS                       = m_pProceduralBLAS;
    Attribs.pBoxData                    = &BoxDataDesc;
    Attribs.BoxDataCount                = 1;
    Attribs.pScratchBuffer              = pScratch;
    Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    m_pImmediateContext->BuildBLAS(Attribs);
}

void Tutorial21_RayTracing::UpdateTLAS()
{
    static constexpr int NumLocalCubes   = 16;
    static constexpr int NumLocalSpheres = 16;
    static constexpr int NumInstances    = NumLocalCubes + NumLocalSpheres + 2; // ground + glass

    bool NeedUpdate = true;

    if (!m_pTLAS)
    {
        TopLevelASDesc TLASDesc;
        TLASDesc.Name             = "TLAS";
        TLASDesc.MaxInstanceCount = NumInstances;
        TLASDesc.Flags            = RAYTRACING_BUILD_AS_ALLOW_UPDATE | RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
        m_pDevice->CreateTLAS(TLASDesc, &m_pTLAS);
        VERIFY_EXPR(m_pTLAS);
        NeedUpdate = false;
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_TLAS")->Set(m_pTLAS);
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_TLAS")->Set(m_pTLAS);
    }

    if (!m_ScratchBuffer)
    {
        BufferDesc B;
        B.Name      = "TLAS Scratch Buffer";
        B.Usage     = USAGE_DEFAULT;
        B.BindFlags = BIND_RAY_TRACING;
        B.Size      = std::max(m_pTLAS->GetScratchBufferSizes().Build, m_pTLAS->GetScratchBufferSizes().Update);
        m_pDevice->CreateBuffer(B, nullptr, &m_ScratchBuffer);
        VERIFY_EXPR(m_ScratchBuffer);
    }

    if (!m_InstanceBuffer)
    {
        BufferDesc B;
        B.Name      = "TLAS Instance Buffer";
        B.Usage     = USAGE_DEFAULT;
        B.BindFlags = BIND_RAY_TRACING;
        B.Size      = TLAS_INSTANCE_DATA_SIZE * NumInstances;
        m_pDevice->CreateBuffer(B, nullptr, &m_InstanceBuffer);
        VERIFY_EXPR(m_InstanceBuffer);
    }

    TLASBuildInstanceData Instances[NumInstances];

    // Cubes around circle
    for (int i = 0; i < NumLocalCubes; ++i)
    {
        auto& inst        = Instances[i];
       // inst.InstanceName = "Cube Instance " + std::to_string(i + 1);
        inst.CustomId     = i % NumTextures;
        inst.pBLAS        = m_pCubeBLAS;
        inst.Mask         = OPAQUE_GEOM_MASK;
        float angle       = 2 * PI_F * i / NumLocalCubes;
        float radius      = 5.0f;
        float x           = std::cos(angle) * radius;
        float y           = std::sin(m_AnimationTime + i) * 1.0f;
        float z           = std::sin(angle) * radius;
        inst.Transform.SetTranslation(x, y, z);
        inst.Transform.SetRotation(float3x3::RotationY(angle + m_AnimationTime).Data());
    }

    // Spheres around larger circle
    for (int i = 0; i < NumLocalSpheres; ++i)
    {
        auto& inst        = Instances[NumLocalCubes + i];
       // inst.InstanceName = "Sphere Instance " + std::to_string(i + 1);
        inst.CustomId     = 0;
        inst.pBLAS        = m_pProceduralBLAS;
        inst.Mask         = OPAQUE_GEOM_MASK;
        float angle       = 2 * PI_F * i / NumLocalSpheres;
        float radius      = 7.0f;
        float x           = std::cos(angle) * radius;
        float z           = std::sin(angle) * radius;
        inst.Transform.SetTranslation(x, -2.0f, z);
    }

    // Ground
    {
        auto& g        = Instances[NumLocalCubes + NumLocalSpheres];
        g.InstanceName = "Ground Instance";
        g.pBLAS        = m_pCubeBLAS;
        g.Mask         = OPAQUE_GEOM_MASK;
        g.Transform.SetRotation(float3x3::Scale(100.0f, 0.1f, 100.0f).Data());
        g.Transform.SetTranslation(0.0f, -6.0f, 0.0f);
    }

    // Glass cube
    {
        auto& gl        = Instances[NumLocalCubes + NumLocalSpheres + 1];
        gl.InstanceName = "Glass Instance";
        gl.pBLAS        = m_pCubeBLAS;
        gl.Mask         = TRANSPARENT_GEOM_MASK;
        gl.Transform.SetRotation(
            (float3x3::Scale(1.5f, 1.5f, 1.5f) *
             float3x3::RotationY(m_AnimationTime * PI_F * 0.25f))
                .Data());
        gl.Transform.SetTranslation(3.0f, -4.0f, -5.0f);
    }

    BuildTLASAttribs Attribs;
    Attribs.pTLAS                        = m_pTLAS;
    Attribs.Update                       = NeedUpdate;
    Attribs.pScratchBuffer               = m_ScratchBuffer;
    Attribs.pInstanceBuffer              = m_InstanceBuffer;
    Attribs.pInstances                   = Instances;
    Attribs.InstanceCount                = NumInstances;
    Attribs.BindingMode                  = HIT_GROUP_BINDING_MODE_PER_INSTANCE;
    Attribs.HitGroupStride               = HIT_GROUP_STRIDE;
    Attribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    m_pImmediateContext->BuildTLAS(Attribs);
}

void Tutorial21_RayTracing::CreateSBT()
{
    static constexpr int NumLocalCubes   = 16;
    static constexpr int NumLocalSpheres = 16;

    ShaderBindingTableDesc SBTDesc;
    SBTDesc.Name = "SBT";
    SBTDesc.pPSO = m_pRayTracingPSO;
    m_pDevice->CreateSBT(SBTDesc, &m_pSBT);
    VERIFY_EXPR(m_pSBT);

    m_pSBT->BindRayGenShader("Main");
    m_pSBT->BindMissShader("PrimaryMiss", PRIMARY_RAY_INDEX);
    m_pSBT->BindMissShader("ShadowMiss", SHADOW_RAY_INDEX);

    for (int i = 0; i < NumLocalCubes; ++i)
        m_pSBT->BindHitGroupForInstance(m_pTLAS,
                                        ("Cube Instance " + std::to_string(i + 1)).c_str(),
                                        PRIMARY_RAY_INDEX, "CubePrimaryHit");

    for (int i = 0; i < NumLocalSpheres; ++i)
        m_pSBT->BindHitGroupForInstance(m_pTLAS,
                                        ("Sphere Instance " + std::to_string(i + 1)).c_str(),
                                        PRIMARY_RAY_INDEX, "SpherePrimaryHit");

    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Ground Instance", PRIMARY_RAY_INDEX, "GroundHit");
    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Glass Instance", PRIMARY_RAY_INDEX, "GlassPrimaryHit");

    m_pSBT->BindHitGroupForTLAS(m_pTLAS, SHADOW_RAY_INDEX, nullptr);

    for (int i = 0; i < NumLocalSpheres; ++i)
        m_pSBT->BindHitGroupForInstance(m_pTLAS,
                                        ("Sphere Instance " + std::to_string(i + 1)).c_str(),
                                        SHADOW_RAY_INDEX, "SphereShadowHit");

    m_pImmediateContext->UpdateSBT(m_pSBT);
}

void Tutorial21_RayTracing::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    if ((m_pDevice->GetAdapterInfo().RayTracing.CapFlags & RAY_TRACING_CAP_FLAG_STANDALONE_SHADERS) == 0)
    {
        UNSUPPORTED("Ray tracing shaders are not supported by device");
        return;
    }

    // Create a buffer with shared constants.
    BufferDesc BuffDesc;
    BuffDesc.Name      = "Constant buffer";
    BuffDesc.Size      = sizeof(m_Constants);
    BuffDesc.Usage     = USAGE_DEFAULT;
    BuffDesc.BindFlags = BIND_UNIFORM_BUFFER;

    m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_ConstantsCB);
    VERIFY_EXPR(m_ConstantsCB != nullptr);

    CreateGraphicsPSO();
    CreateRayTracingPSO();
    LoadTextures();
    CreateCubeBLAS();
    CreateProceduralBLAS();
    UpdateTLAS();
    CreateSBT();

    // Setup camera.
    m_Camera.SetPos(float3(7.f, -0.5f, -16.5f));
    m_Camera.SetRotation(0.48f, -0.145f);
    m_Camera.SetRotationSpeed(0.005f);
    m_Camera.SetMoveSpeed(5.f);
    m_Camera.SetSpeedUpScales(5.f, 10.f);

    // Initialize constants.
    {
        m_Constants.ClipPlanes   = float2{0.1f, 100.0f};
        m_Constants.ShadowPCF    = 1;
        m_Constants.MaxRecursion = std::min(Uint32{6}, m_MaxRecursionDepth);

        // Sphere constants.
        m_Constants.SphereReflectionColorMask = {0.81f, 1.0f, 0.45f};
        m_Constants.SphereReflectionBlur      = 1;

        // Glass cube constants.
        m_Constants.GlassReflectionColorMask = {0.22f, 0.83f, 0.93f};
        m_Constants.GlassAbsorption          = 0.5f;
        m_Constants.GlassMaterialColor       = {0.33f, 0.93f, 0.29f};
        m_Constants.GlassIndexOfRefraction   = {1.5f, 1.02f};
        m_Constants.GlassEnableDispersion    = 0;

        // Wavelength to RGB and index of refraction interpolation factor.
        m_Constants.DispersionSamples[0]  = {0.140000f, 0.000000f, 0.266667f, 0.53f};
        m_Constants.DispersionSamples[1]  = {0.130031f, 0.037556f, 0.612267f, 0.25f};
        m_Constants.DispersionSamples[2]  = {0.100123f, 0.213556f, 0.785067f, 0.16f};
        m_Constants.DispersionSamples[3]  = {0.050277f, 0.533556f, 0.785067f, 0.00f};
        m_Constants.DispersionSamples[4]  = {0.000000f, 0.843297f, 0.619682f, 0.13f};
        m_Constants.DispersionSamples[5]  = {0.000000f, 0.927410f, 0.431834f, 0.38f};
        m_Constants.DispersionSamples[6]  = {0.000000f, 0.972325f, 0.270893f, 0.27f};
        m_Constants.DispersionSamples[7]  = {0.000000f, 0.978042f, 0.136858f, 0.19f};
        m_Constants.DispersionSamples[8]  = {0.324000f, 0.944560f, 0.029730f, 0.47f};
        m_Constants.DispersionSamples[9]  = {0.777600f, 0.871879f, 0.000000f, 0.64f};
        m_Constants.DispersionSamples[10] = {0.972000f, 0.762222f, 0.000000f, 0.77f};
        m_Constants.DispersionSamples[11] = {0.971835f, 0.482222f, 0.000000f, 0.62f};
        m_Constants.DispersionSamples[12] = {0.886744f, 0.202222f, 0.000000f, 0.73f};
        m_Constants.DispersionSamples[13] = {0.715967f, 0.000000f, 0.000000f, 0.68f};
        m_Constants.DispersionSamples[14] = {0.459920f, 0.000000f, 0.000000f, 0.91f};
        m_Constants.DispersionSamples[15] = {0.218000f, 0.000000f, 0.000000f, 0.99f};
        m_Constants.DispersionSampleCount = 4;

        m_Constants.AmbientColor  = float4(1.f, 1.f, 1.f, 0.f) * 0.015f;
        m_Constants.LightPos[0]   = {8.00f, +8.0f, +0.00f, 0.f};
        m_Constants.LightColor[0] = {1.00f, +0.8f, +0.80f, 0.f};
        m_Constants.LightPos[1]   = {0.00f, +4.0f, -5.00f, 0.f};
        m_Constants.LightColor[1] = {0.85f, +1.0f, +0.85f, 0.f};

        // Random points on disc.
        m_Constants.DiscPoints[0] = {+0.0f, +0.0f, +0.9f, -0.9f};
        m_Constants.DiscPoints[1] = {-0.8f, +1.0f, -1.1f, -0.8f};
        m_Constants.DiscPoints[2] = {+1.5f, +1.2f, -2.1f, +0.7f};
        m_Constants.DiscPoints[3] = {+0.1f, -2.2f, -0.2f, +2.4f};
        m_Constants.DiscPoints[4] = {+2.4f, -0.3f, -3.0f, +2.8f};
        m_Constants.DiscPoints[5] = {+2.0f, -2.6f, +0.7f, +3.5f};
        m_Constants.DiscPoints[6] = {-3.2f, -1.6f, +3.4f, +2.2f};
        m_Constants.DiscPoints[7] = {-1.8f, -3.2f, -1.1f, +3.6f};
    }
    static_assert(sizeof(HLSL::Constants) % 16 == 0, "must be aligned by 16 bytes");
}

void Tutorial21_RayTracing::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    SampleBase::ModifyEngineInitInfo(Attribs);

    // Require ray tracing feature.
    Attribs.EngineCI.Features.RayTracing = DEVICE_FEATURE_STATE_ENABLED;
}

// Render a frame
void Tutorial21_RayTracing::Render()
{
    UpdateTLAS();

    // Update constants
    {
        float3   CameraWorldPos = float3::MakeVector(m_Camera.GetWorldMatrix()[3]);
        float4x4 CameraViewProj = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();

        m_Constants.CameraPos   = float4{CameraWorldPos, 1.0f};
        m_Constants.InvViewProj = CameraViewProj.Inverse();

        m_pImmediateContext->UpdateBuffer(m_ConstantsCB, 0, sizeof(m_Constants), &m_Constants, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Trace rays
    {
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_ColorBuffer")->Set(m_pColorRT->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));

        m_pImmediateContext->SetPipelineState(m_pRayTracingPSO);
        m_pImmediateContext->CommitShaderResources(m_pRayTracingSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        TraceRaysAttribs Attribs;
        Attribs.DimensionX = m_pColorRT->GetDesc().Width;
        Attribs.DimensionY = m_pColorRT->GetDesc().Height;
        Attribs.pSBT       = m_pSBT;

        m_pImmediateContext->TraceRays(Attribs);
    }

    // Blit to swapchain image
    {
        m_pImageBlitSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_pColorRT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        m_pImmediateContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->SetPipelineState(m_pImageBlitPSO);
        m_pImmediateContext->CommitShaderResources(m_pImageBlitSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    }
}

void Tutorial21_RayTracing::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime);

    if (m_Animate)
    {
        m_AnimationTime += static_cast<float>(std::min(m_MaxAnimationTimeDelta, ElapsedTime));
    }

    m_Camera.Update(m_InputController, static_cast<float>(ElapsedTime));

    // Do not allow going underground
    float3 oldPos = m_Camera.GetPos();
    if (oldPos.y < -5.7f)
    {
        oldPos.y = -5.7f;
        m_Camera.SetPos(oldPos);
        m_Camera.Update(m_InputController, 0.f);
    }
}

void Tutorial21_RayTracing::WindowResize(Uint32 Width, Uint32 Height)
{
    if (Width == 0 || Height == 0)
        return;

    // Update projection matrix.
    float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    m_Camera.SetProjAttribs(m_Constants.ClipPlanes.x, m_Constants.ClipPlanes.y, AspectRatio, PI_F / 4.f,
                            m_pSwapChain->GetDesc().PreTransform, m_pDevice->GetDeviceInfo().NDC.MinZ == -1);

    // Check if the image needs to be recreated.
    if (m_pColorRT != nullptr &&
        m_pColorRT->GetDesc().Width == Width &&
        m_pColorRT->GetDesc().Height == Height)
        return;

    m_pColorRT = nullptr;

    // Create window-size color image.
    TextureDesc RTDesc       = {};
    RTDesc.Name              = "Color buffer";
    RTDesc.Type              = RESOURCE_DIM_TEX_2D;
    RTDesc.Width             = Width;
    RTDesc.Height            = Height;
    RTDesc.BindFlags         = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;
    RTDesc.ClearValue.Format = m_ColorBufferFormat;
    RTDesc.Format            = m_ColorBufferFormat;

    m_pDevice->CreateTexture(RTDesc, nullptr, &m_pColorRT);
}

void Tutorial21_RayTracing::UpdateUI()
{
    const float MaxIndexOfRefraction = 2.0f;
    const float MaxDispersion        = 0.5f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Checkbox("Animate", &m_Animate);

        ImGui::Text("Use WASD to move camera");
        ImGui::SliderInt("Shadow blur", &m_Constants.ShadowPCF, 0, 16);
        ImGui::SliderInt("Max recursion", &m_Constants.MaxRecursion, 0, m_MaxRecursionDepth);

        // Ahora mostramos 16 checkboxes, uno por cada cubo
        for (int i = 0; i < NumCubes; ++i)
        {
            ImGui::Checkbox(("Cube " + std::to_string(i + 1)).c_str(), &m_EnableCubes[i]);
            if ((i + 1) % 8 != 0) // ejemplo de dos filas de 8
                ImGui::SameLine();
        }

        ImGui::Separator();
        ImGui::Text("Glass cube");
        ImGui::Checkbox("Dispersion", &m_Constants.GlassEnableDispersion);

        ImGui::SliderFloat("Index of refraction", &m_Constants.GlassIndexOfRefraction.x, 1.0f, MaxIndexOfRefraction);

        if (m_Constants.GlassEnableDispersion)
        {
            ImGui::SliderFloat("Dispersion factor", &m_DispersionFactor, 0.0f, MaxDispersion);
            m_Constants.GlassIndexOfRefraction.y = m_Constants.GlassIndexOfRefraction.x + m_DispersionFactor;

            int rsamples = PlatformMisc::GetLSB(m_Constants.DispersionSampleCount);
            ImGui::SliderInt("Dispersion samples", &rsamples, 1, PlatformMisc::GetLSB(Uint32{MAX_DISPERS_SAMPLES}), std::to_string(1 << rsamples).c_str());
            m_Constants.DispersionSampleCount = 1u << rsamples;
        }

        ImGui::ColorEdit3("Reflection color", m_Constants.GlassReflectionColorMask.Data(), ImGuiColorEditFlags_NoAlpha);
        ImGui::ColorEdit3("Material color", m_Constants.GlassMaterialColor.Data(), ImGuiColorEditFlags_NoAlpha);
        ImGui::SliderFloat("Absorption", &m_Constants.GlassAbsorption, 0.0f, 2.0f);

        ImGui::Separator();
        ImGui::Text("Sphere");
        ImGui::SliderInt("Reflection blur", &m_Constants.SphereReflectionBlur, 1, 16);
        ImGui::ColorEdit3("Color mask", m_Constants.SphereReflectionColorMask.Data(), ImGuiColorEditFlags_NoAlpha);
    }
    ImGui::End();
}

} // namespace Diligent
