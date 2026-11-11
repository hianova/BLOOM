#ifndef FLOW_SHADER_PORTABILITY_H
#define FLOW_SHADER_PORTABILITY_H

/*
 * ============================================================================
 * FLOW SINGLE-SOURCE SHADER PORTABILITY LAYER (Metal / Vulkan / HLSL / C17)
 * ============================================================================
 * Treats FlowGpuFrameUniforms256 (256B) + FlowEngineBus1K (1024B, 16 x 64B)
 * as a Zero-Serialization Cross-GPU Register Bridge:
 *   - 0 Texture Bindings / 0 Sampler Descriptors / 100% Pure Analytic SDF Math
 *   - Portable address-space macros (FLOW_CONST_REF, FLOW_THREAD_REF, FLOW_OUT)
 *   - 32-Bit Pair Wave64 Bitmask Evaluator (0 shader uint64_t required on WebGPU/Vulkan)
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
  #define FLOW_CONST_REF(type)              constant type &
  #define FLOW_THREAD_CREF(type)            thread const type &
  #define FLOW_OUT_F32(name)                thread float &name
#elif defined(FLOW_SHADER_TARGET_VULKAN_GLSL)
  /* 2. Steam Deck (SteamOS AMD RDNA2) & Switch Vulkan SPIR-V */
  #define FLOW_SHADER_BACKEND_NAME          "Vulkan-SPIRV-RDNA2-NVN"
  #define FLOW_GPU_INLINE                   inline
  #define FLOW_BUF_RO(type, name, slot)     layout(std430, binding = slot) readonly buffer _buf_##name { type name[]; }
  #define FLOW_UNIFORM_RO(type, name, slot) layout(std140, binding = slot) uniform _ub_##name { type name; }
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
  #define FLOW_CONST_REF(type)              const type
  #define FLOW_THREAD_CREF(type)            const type
  #define FLOW_OUT_F32(name)                out float name
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
  #define FLOW_CONST_REF(type)              const type
  #define FLOW_THREAD_CREF(type)            const type
  #define FLOW_OUT_F32(name)                out float name
  #define mix(a, b, t)                      lerp((a), (b), (t))
  #define fract(x)                          frac(x)
#elif defined(FLOW_SHADER_TARGET_WEBGPU_WGSL)
  /* 4. WebGPU (Dawn / wgpu) Portable 32-Bit Pair Compute Bridge */
  #define FLOW_SHADER_BACKEND_NAME          "WebGPU-WGSL-U32x2"
  #define FLOW_GPU_INLINE                   static inline
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
  #define FLOW_CONST_REF(type)              const type *
  #define FLOW_THREAD_CREF(type)            const type *
  #define FLOW_OUT_F32(name)                float *name
#else
  /* 5. Pure C17 Host Compiler / FLOWY Static Analyzer */
  #define FLOW_SHADER_BACKEND_NAME          "C17-Host-JSI-Bridge"
  #define FLOW_GPU_INLINE                   static inline
  #define FLOW_THREAD_ADDR
  #define FLOW_CONSTANT_ADDR                const
  #define FLOW_CONST_REF(type)              const type *
  #define FLOW_THREAD_CREF(type)            const type *
  #define FLOW_OUT_F32(name)                float *name
#endif

#define FLOW_JSI_BUS1K_BINDING_SLOT         3U
#define FLOW_JSI_BUS1K_TOTAL_BYTES          1024U
#define FLOW_JSI_BUS1K_SLOT_COUNT           16U

/*
 * Portable 32-Bit Pair Wave64 Tile Bitmask Test & 64-Bit Carry-Propagating Algebra:
 * Evaluates 64-bit Wave64 masks and Bus1K fences split into two 32-bit unsigned words (lo32, hi32),
 * eliminating the need for 64-bit integer shader extensions on WebGPU (WGSL), Vulkan & DX12.
 */
#if defined(__METAL_VERSION__) || defined(FLOW_SHADER_TARGET_VULKAN_GLSL) || defined(FLOW_SHADER_TARGET_HLSL)
FLOW_GPU_INLINE bool flow_wave64_mask_test_u32x2(uint lo32, uint hi32, uint bit_idx) {
    uint word = (bit_idx < 32U) ? lo32 : hi32;
    uint shift = bit_idx & 31U;
    return ((word >> shift) & 1U) != 0U;
}

FLOW_GPU_INLINE uint flow_u32x2_add_lo(uint a_lo, uint b_lo) {
    return a_lo + b_lo;
}

FLOW_GPU_INLINE uint flow_u32x2_add_hi(uint a_lo, uint a_hi, uint b_lo, uint b_hi) {
    uint sum_lo = a_lo + b_lo;
    uint carry = (sum_lo < a_lo) ? 1U : 0U;
    return a_hi + b_hi + carry;
}
#endif

#endif /* FLOW_SHADER_PORTABILITY_H */
