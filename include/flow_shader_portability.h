#ifndef FLOW_SHADER_PORTABILITY_H
#define FLOW_SHADER_PORTABILITY_H

/*
 * ============================================================================
 * FLOW SINGLE-SOURCE SHADER PORTABILITY LAYER (macOS Metal / Steam Deck Vulkan / Switch NVN)
 * ============================================================================
 * Treats FlowEngineBus1K (1024 Bytes, 16 x 64B slots) as a React Native JSI
 * Zero-Serialization Bridge:
 *   - 0 Texture Bindings / 0 Sampler Descriptors / 100% Pure Analytic SDF Math
 *   - Cross-compiles identical SDF raymarching & Physical Liquid-Glass optics via
 *     Metal AIR (macOS), SPIRV-Cross / SDL3_shadercross (Steam Deck RDNA2 Vulkan),
 *     and NVN / Vulkan SPIR-V (Nintendo Switch).
 * ============================================================================
 */

#if defined(__METAL_VERSION__)
  /* 1. Apple Metal Shading Language (macOS / iPadOS / Apple Silicon) */
  #define FLOW_SHADER_BACKEND_NAME          "Apple-Metal-AIR"
  #define FLOW_GPU_INLINE                   static inline __attribute__((always_inline))
  #define FLOW_BUF_RO(type, name, slot)     constant type *name [[buffer(slot)]]
  #define FLOW_UNIFORM_RO(type, name, slot) constant type &name [[buffer(slot)]]
  #define FLOW_THREAD_ADDR                  thread
  #define FLOW_CONSTANT_ADDR                constant
#elif defined(FLOW_SHADER_TARGET_VULKAN_GLSL)
  /* 2. Steam Deck (SteamOS AMD RDNA2) & Switch Vulkan SPIR-V */
  #define FLOW_SHADER_BACKEND_NAME          "Vulkan-SPIRV-RDNA2-NVN"
  #define FLOW_GPU_INLINE                   inline
  #define FLOW_BUF_RO(type, name, slot)     layout(std430, binding = slot) readonly buffer _buf_##name { type name[]; }
  #define FLOW_UNIFORM_RO(type, name, slot) layout(std140, binding = slot) uniform _ub_##name { type name; }
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
  #define float2                            vec2
  #define float3                            vec3
  #define float4                            vec4
  #define uint                              uint32_t
  #define ulong                             uint64_t
#elif defined(FLOW_SHADER_TARGET_HLSL)
  /* 3. DirectX 12 / SDL3_shadercross HLSL Source */
  #define FLOW_SHADER_BACKEND_NAME          "HLSL-SM6-SDL3-ShaderCross"
  #define FLOW_GPU_INLINE                   inline
  #define FLOW_BUF_RO(type, name, slot)     StructuredBuffer<type> name : register(t##slot)
  #define FLOW_UNIFORM_RO(type, name, slot) ConstantBuffer<type> name : register(b##slot)
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
  #define mix(a, b, t)                      lerp((a), (b), (t))
  #define fract(x)                          frac(x)
#else
  /* 4. Pure C17 Host Compiler / FLOWY Static Analyzer */
  #define FLOW_SHADER_BACKEND_NAME          "C17-Host-JSI-Bridge"
  #define FLOW_GPU_INLINE                   static inline
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
#endif

#define FLOW_JSI_BUS1K_BINDING_SLOT         3U
#define FLOW_JSI_BUS1K_TOTAL_BYTES          1024U
#define FLOW_JSI_BUS1K_SLOT_COUNT           16U

#endif /* FLOW_SHADER_PORTABILITY_H */
